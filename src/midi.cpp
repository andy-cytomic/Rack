#include <map>
#include <utility>

#include <midi.hpp>
#include <string.hpp>
#include <system.hpp>


namespace rack {
namespace midi {


static std::vector<std::pair<int, Driver*>> drivers;

////////////////////
// Device
////////////////////

void InputDevice::subscribe(Input* input) {
	subscribed.insert(input);
}

void InputDevice::unsubscribe(Input* input) {
	// Remove Input from subscriptions
	auto it = subscribed.find(input);
	if (it != subscribed.end())
		subscribed.erase(it);
}

void InputDevice::onMessage(const Message &message) {
	// Set timestamp to now if unset
	Message msg = message;
	if (msg.timestamp == 0.0)
		msg.timestamp = system::getTime();

	for (Input* input : subscribed) {
		// We're probably in the MIDI driver's thread, so set the Rack context.
		contextSet(input->context);
		// Filter channel if message is not a system MIDI message
		if (msg.getStatus() != 0xf && input->channel >= 0 && msg.getChannel() != input->channel)
			continue;
		// Pass message to Input port
		input->onMessage(msg);
	}
}

void OutputDevice::subscribe(Output* output) {
	subscribed.insert(output);
}

void OutputDevice::unsubscribe(Output* output) {
	auto it = subscribed.find(output);
	if (it != subscribed.end())
		subscribed.erase(it);
}

////////////////////
// Port
////////////////////

Port::Port() {
	context = contextGet();
}

Port::~Port() {
}

Driver* Port::getDriver() {
	return driver;
}

int Port::getDriverId() {
	return driverId;
}

void Port::setDriverId(int driverId) {
	// Unset device and driver
	setDeviceId(-1);
	driver = NULL;
	this->driverId = -1;

	// Find driver by ID
	driver = midi::getDriver(driverId);
	if (driver) {
		this->driverId = driverId;
	}
	else {
		// Set first driver as default
		driver = drivers[0].second;
		this->driverId = drivers[0].first;
	}
}

Device* Port::getDevice() {
	return device;
}

int Port::getDeviceId() {
	return deviceId;
}

int Port::getChannel() {
	return channel;
}

void Port::setChannel(int channel) {
	this->channel = channel;
}

std::string Port::getChannelName(int channel) {
	if (channel < 0)
		return "All channels";
	else
		return string::f("Channel %d", channel + 1);
}

json_t* Port::toJson() {
	json_t* rootJ = json_object();
	json_object_set_new(rootJ, "driver", json_integer(getDriverId()));

	if (device) {
		std::string deviceName = device->getName();
		if (!deviceName.empty())
			json_object_set_new(rootJ, "deviceName", json_string(deviceName.c_str()));
	}

	json_object_set_new(rootJ, "channel", json_integer(getChannel()));
	return rootJ;
}

void Port::fromJson(json_t* rootJ) {
	setDriverId(-1);

	json_t* driverJ = json_object_get(rootJ, "driver");
	if (driverJ)
		setDriverId(json_integer_value(driverJ));

	if (driver) {
		json_t* deviceNameJ = json_object_get(rootJ, "deviceName");
		if (deviceNameJ) {
			std::string deviceName = json_string_value(deviceNameJ);
			// Search for device ID with equal name
			for (int deviceId : getDeviceIds()) {
				if (getDeviceName(deviceId) == deviceName) {
					setDeviceId(deviceId);
					break;
				}
			}
		}
	}

	json_t* channelJ = json_object_get(rootJ, "channel");
	if (channelJ)
		channel = json_integer_value(channelJ);
}

////////////////////
// Input
////////////////////

Input::Input() {
	reset();
}

Input::~Input() {
	setDriverId(-1);
}

void Input::reset() {
	setDriverId(-1);
	channel = -1;
}

std::vector<int> Input::getDeviceIds() {
	if (!driver)
		return {};
	try {
		return driver->getInputDeviceIds();
	}
	catch (Exception& e) {
		WARN("MIDI port could not get input device IDs: %s", e.what());
		return {};
	}
}

void Input::setDeviceId(int deviceId) {
	// Destroy device
	if (driver && this->deviceId >= 0) {
		try {
			driver->unsubscribeInput(this->deviceId, this);
		}
		catch (Exception& e) {
			WARN("MIDI port could not unsubscribe from input: %s", e.what());
		}
	}
	device = inputDevice = NULL;
	this->deviceId = -1;

	// Create device
	if (driver && deviceId >= 0) {
		try {
			device = inputDevice = driver->subscribeInput(deviceId, this);
			this->deviceId = deviceId;
		}
		catch (Exception& e) {
			WARN("MIDI port could not subscribe to input: %s", e.what());
		}
	}
}

std::string Input::getDeviceName(int deviceId) {
	if (!driver)
		return "";
	try {
		return driver->getInputDeviceName(deviceId);
	}
	catch (Exception& e) {
		WARN("MIDI port could not get input device name: %s", e.what());
		return "";
	}
}

std::vector<int> Input::getChannels() {
	std::vector<int> channels;
	for (int c = -1; c < 16; c++) {
		channels.push_back(c);
	}
	return channels;
}

void InputQueue::onMessage(const Message &message) {
	if ((int) queue.size() >= queueMaxSize)
		return;
	// Push to queue
	queue.push(message);
}

////////////////////
// Output
////////////////////

Output::Output() {
	reset();
}

Output::~Output() {
	setDriverId(-1);
}

void Output::reset() {
	setDriverId(-1);
	channel = 0;
}

std::vector<int> Output::getDeviceIds() {
	if (!driver)
		return {};
	try {
		return driver->getOutputDeviceIds();
	}
	catch (Exception& e) {
		WARN("MIDI port could not get output device IDs: %s", e.what());
		return {};
	}
}

void Output::setDeviceId(int deviceId) {
	// Destroy device
	if (driver && this->deviceId >= 0) {
		try {
			driver->unsubscribeOutput(this->deviceId, this);
		}
		catch (Exception& e) {
			WARN("MIDI port could not unsubscribe from output: %s", e.what());
		}
	}
	device = outputDevice = NULL;
	this->deviceId = -1;

	// Create device
	if (driver && deviceId >= 0) {
		try {
			device = outputDevice = driver->subscribeOutput(deviceId, this);
			this->deviceId = deviceId;
		}
		catch (Exception& e) {
			WARN("MIDI port could not subscribe to output: %s", e.what());
		}
	}
}

std::string Output::getDeviceName(int deviceId) {
	if (driver)
		return "";
	try {
		return driver->getOutputDeviceName(deviceId);
	}
	catch (Exception& e) {
		WARN("MIDI port could not get output device name: %s", e.what());
		return "";
	}
}

std::vector<int> Output::getChannels() {
	std::vector<int> channels;
	for (int c = 0; c < 16; c++) {
		channels.push_back(c);
	}
	return channels;
}

void Output::sendMessage(const Message &message) {
	if (!outputDevice)
		return;

	// Set channel if message is not a system MIDI message
	Message msg = message;
	if (msg.getStatus() != 0xf && channel >= 0) {
		msg.setChannel(channel);
	}
	// DEBUG("sendMessage %02x %02x %02x", msg.cmd, msg.data1, msg.data2);
	try {
		outputDevice->sendMessage(msg);
	}
	catch (Exception& e) {
		// Don't log error because it could flood the log.
		// WARN("MIDI port could not be sent MIDI message: %s", e.what());
		// TODO Perhaps `setDevice(-1)` if sending message fails?
	}
}


////////////////////
// midi
////////////////////

void init() {
}

void destroy() {
	for (auto& pair : drivers) {
		delete pair.second;
	}
	drivers.clear();
}

void addDriver(int driverId, Driver* driver) {
	assert(driver);
	drivers.push_back(std::make_pair(driverId, driver));
}

std::vector<int> getDriverIds() {
	std::vector<int> driverIds;
	for (auto& pair : drivers) {
		driverIds.push_back(pair.first);
	}
	return driverIds;
}

Driver* getDriver(int driverId) {
	// Search for driver by ID
	for (auto& pair : drivers) {
		if (pair.first == driverId)
			return pair.second;
	}
	return NULL;
}


} // namespace midi
} // namespace rack
