// The controller on the SIO0 port, driven the way a game drives it: bytes written to JOY_DATA, replies read back, the pad's /ACK watched in JOY_STAT.
// Checks byte-exact transactions: the digital and analog poll, the configuration (escape) mode of a DualShock with its status and constant
// queries, setting the analog mode and its lock, the rumble mapping, and when the pad is and is not selected and acknowledges.
// Needs no BIOS and no disc. There is no host gamepad in a test run (the pad is only opened when an SDL "device added" event reaches it), so
// every poll answers "nothing pressed, sticks centred".
//
// What is checked is what the hardware documents (psx-spx "Controllers and Memory Cards", "Controller Configuration (DualShock)"): the pad answers
// 01h 42h 00h with Hi-Z, its ID, 5Ah and then the data; it pulls /ACK after every byte but the last; the ID is 41h digital, 73h analog and F3h in
// configuration mode. Left out on purpose because the hardware behaviour is not confirmed: what the pad does with a command it does not know, the
// reply to a poll sent while in configuration mode, the reply to the first rumble-mapping command, and the exact cycle at which /ACK is pulled.
#include "io/SIO0.h"
#include "support/captured_output.h"
#include "support/echo_check.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

using echo_check::check;
using echo_check::hex;

namespace {

constexpr uint32_t JOY_DATA = 0x1F801040;
constexpr uint32_t JOY_STAT = 0x1F801044;
constexpr uint32_t JOY_MODE = 0x1F801048;
constexpr uint32_t JOY_CTRL = 0x1F80104A;
constexpr uint32_t JOY_BAUD = 0x1F80104E;

constexpr uint16_t STAT_DSR = 1u << 7;                // the pad's /ACK, as the console sees it

// JOY_CTRL the way the BIOS writes it for port 1: transmit enable, DTR (this is what selects the pad) and the DSR interrupt enable
constexpr uint16_t CTRL_SELECTED = 0x1003;
constexpr uint16_t CTRL_DESELECTED = 0x1001;          // DTR low
constexpr uint16_t CTRL_PORT2_SELECTED = 0x3003;      // the same with the port 2 select bit (13)
constexpr uint16_t MODE_BIOS = 0x000D;
constexpr uint16_t BAUD_BIOS = 0x0088;                // one byte takes 8 * 0x88 = 1088 CPU cycles at this setting
constexpr int CYCLES_PER_BYTE = 3000;                 // the byte, its reply and the /ACK pulse are all done well within this

using Bytes = std::vector<uint8_t>;

std::string show(const Bytes& bytes) {
	std::string text;
	for (uint8_t byte : bytes) {
		if (!text.empty()) text += " ";
		text += hex(byte, 2);
	}
	return text.empty() ? "(none)" : text;
}

struct Packet {
	Bytes replies;
	std::vector<int> acknowledges;   // how many times /ACK was pulled after each byte
};

// A console with a pad on port 1
class Port {
public:
	Port() {
		sio.write16(JOY_BAUD, BAUD_BIOS);
		sio.write16(JOY_MODE, MODE_BIOS);
	}

	DualShock& pad() { return sio.getController(); }

	// Sends one packet the way a game does: select, one byte at a time waiting for each reply, deselect
	Packet packet(const Bytes& bytes, uint16_t selectValue = CTRL_SELECTED) {
		Packet result;
		sio.write16(JOY_CTRL, selectValue);
		for (uint8_t byte : bytes) {
			sio.write8(JOY_DATA, byte);
			int pulses = 0;
			bool dsrWasHigh = (sio.read16(JOY_STAT) & STAT_DSR) != 0;
			for (int cycle = 0; cycle < CYCLES_PER_BYTE; cycle++) {
				sio.advanceClock();
				bool dsrHigh = (sio.read16(JOY_STAT) & STAT_DSR) != 0;
				if (dsrHigh && !dsrWasHigh) pulses++;
				dsrWasHigh = dsrHigh;
			}
			result.replies.push_back(sio.read8(JOY_DATA));
			result.acknowledges.push_back(pulses);
		}
		sio.write16(JOY_CTRL, CTRL_DESELECTED);
		return result;
	}

	SIO0 sio;
};

// 01h <command> 00h <parameters...>, padded with zeros to `length` bytes
Bytes command(uint8_t commandByte, const Bytes& parameters, size_t length) {
	Bytes bytes = {0x01, commandByte, 0x00};
	bytes.insert(bytes.end(), parameters.begin(), parameters.end());
	while (bytes.size() < length) bytes.push_back(0x00);
	return bytes;
}

// Checks the replies (only as many as `expected` lists, so a reply whose tail is not confirmed can be left out) and that the pad pulled /ACK
// exactly once after each of the first `acknowledged` bytes and not at all after the others.
void checkPacket(const std::string& name, const Packet& packet, const Bytes& expected, size_t acknowledged) {
	Bytes got(packet.replies.begin(), packet.replies.begin() + static_cast<std::ptrdiff_t>(std::min(expected.size(), packet.replies.size())));
	check(got == expected, name + ": replies", "got " + show(got) + ", expected " + show(expected));

	bool acknowledgesMatch = true;
	std::string pattern;
	for (size_t index = 0; index < packet.acknowledges.size(); index++) {
		int wanted = index < acknowledged ? 1 : 0;
		if (packet.acknowledges[index] != wanted) acknowledgesMatch = false;
		pattern += std::to_string(packet.acknowledges[index]);
	}
	std::string wantedPattern;
	for (size_t index = 0; index < packet.acknowledges.size(); index++) wantedPattern += index < acknowledged ? "1" : "0";
	check(acknowledgesMatch, name + ": /ACK pulled after every byte but the last", "pulses per byte " + pattern + ", expected " + wantedPattern);
}

// A full packet of n bytes: every byte acknowledged but the last
void checkFullPacket(const std::string& name, const Packet& packet, const Bytes& expected) {
	checkPacket(name, packet, expected, packet.replies.size() - 1);
}

void enterConfiguration(Port& port, size_t length = 5) {
	port.packet(command(0x43, {0x01}, length));
}

void exitConfiguration(Port& port) {
	port.packet(command(0x43, {0x00}, 9));
}

void checkDigitalPoll() {
	CapturedOutput quiet;
	Port port;
	Packet packet = port.packet(command(0x42, {}, 5));
	checkFullPacket("digital poll", packet, {0xFF, 0x41, 0x5A, 0xFF, 0xFF});   // Hi-Z, ID, 5Ah, then no button pressed
}

void checkAnalogPoll() {
	CapturedOutput quiet;
	Port port;
	port.pad().setAnalogMode(true, false);
	Packet packet = port.packet(command(0x42, {}, 9));
	checkFullPacket("analog poll", packet, {0xFF, 0x73, 0x5A, 0xFF, 0xFF, 0x80, 0x80, 0x80, 0x80});   // buttons, then the sticks centred
}

void checkConfigurationEnterAndExit() {
	CapturedOutput quiet;
	Port port;
	Packet enter = port.packet(command(0x43, {0x01}, 5));
	checkFullPacket("enter configuration (from digital)", enter, {0xFF, 0x41, 0x5A, 0xFF, 0xFF});
	check(port.pad().isConfigurationMode(), "enter configuration: the pad is in configuration mode afterwards");

	Packet status = port.packet(command(0x45, {}, 9));
	check(status.replies.size() == 9 && status.replies[1] == 0xF3, "in configuration mode the ID is F3h", "ID read " + hex(status.replies[1], 2));

	Packet leave = port.packet(command(0x43, {0x00}, 9));
	checkPacket("exit configuration", leave, {0xFF, 0xF3, 0x5A}, 8);   // the exit takes effect after the packet, so this one still answers F3h
	check(!port.pad().isConfigurationMode(), "exit configuration: the pad is out of configuration mode afterwards");

	Packet poll = port.packet(command(0x42, {}, 5));
	checkFullPacket("poll after leaving configuration mode", poll, {0xFF, 0x41, 0x5A, 0xFF, 0xFF});
}

// The status and the constants a game reads to identify a DualShock
void checkConfigurationQueries() {
	CapturedOutput quiet;
	Port port;
	enterConfiguration(port);

	checkFullPacket("get status (digital mode)", port.packet(command(0x45, {}, 9)), {0xFF, 0xF3, 0x5A, 0x03, 0x02, 0x00, 0x02, 0x01, 0x00});
	checkFullPacket("get constant 1, parameter 0", port.packet(command(0x46, {0x00}, 9)), {0xFF, 0xF3, 0x5A, 0x00, 0x00, 0x01, 0x02, 0x00, 0x0A});
	checkFullPacket("get constant 1, parameter 1", port.packet(command(0x46, {0x01}, 9)), {0xFF, 0xF3, 0x5A, 0x00, 0x00, 0x01, 0x01, 0x01, 0x14});
	checkFullPacket("get constant 2", port.packet(command(0x47, {0x00}, 9)), {0xFF, 0xF3, 0x5A, 0x00, 0x00, 0x02, 0x00, 0x01, 0x00});
	checkFullPacket("get constant 3, parameter 0", port.packet(command(0x4C, {0x00}, 9)), {0xFF, 0xF3, 0x5A, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00});
	checkFullPacket("get constant 3, parameter 1", port.packet(command(0x4C, {0x01}, 9)), {0xFF, 0xF3, 0x5A, 0x00, 0x00, 0x00, 0x07, 0x00, 0x00});
}

// Setting the analog mode through configuration mode, and the lock that stops the Analog button changing it
void checkModeAndLock() {
	CapturedOutput quiet;
	Port port;

	// Analog, not locked
	enterConfiguration(port);
	checkFullPacket("set mode analog", port.packet(command(0x44, {0x01, 0x00}, 9)), {0xFF, 0xF3, 0x5A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00});
	check(port.pad().isAnalogMode() && !port.pad().isAnalogLocked(), "set mode analog: the pad is analog and not locked");
	checkFullPacket("get status (analog mode): the LED is on", port.packet(command(0x45, {}, 9)), {0xFF, 0xF3, 0x5A, 0x03, 0x02, 0x01, 0x02, 0x01, 0x00});
	exitConfiguration(port);
	checkFullPacket("poll in analog mode", port.packet(command(0x42, {}, 9)), {0xFF, 0x73, 0x5A, 0xFF, 0xFF, 0x80, 0x80, 0x80, 0x80});
	port.pad().toggleAnalogMode();
	check(!port.pad().isAnalogMode(), "the Analog button switches an unlocked pad to digital");
	checkFullPacket("poll after the Analog button", port.packet(command(0x42, {}, 5)), {0xFF, 0x41, 0x5A, 0xFF, 0xFF});

	// Analog, locked: the Analog button no longer works
	enterConfiguration(port);
	port.packet(command(0x44, {0x01, 0x03}, 9));
	exitConfiguration(port);
	check(port.pad().isAnalogMode() && port.pad().isAnalogLocked(), "set mode analog with lock: the pad is analog and locked");
	port.pad().toggleAnalogMode();
	check(port.pad().isAnalogMode(), "the Analog button does nothing on a locked pad");

	// A game can set the mode back to digital and unlock it
	enterConfiguration(port, 9);
	port.packet(command(0x44, {0x00, 0x00}, 9));
	exitConfiguration(port);
	check(!port.pad().isAnalogMode() && !port.pad().isAnalogLocked(), "set mode digital without lock: the pad is digital and unlocked");
	checkFullPacket("poll back in digital mode", port.packet(command(0x42, {}, 5)), {0xFF, 0x41, 0x5A, 0xFF, 0xFF});
}

// The rumble mapping: which byte of a poll drives which motor
void checkRumbleMapping() {
	CapturedOutput quiet;
	Port port;
	enterConfiguration(port);
	port.packet(command(0x4D, {0x00, 0x01, 0xFF, 0xFF, 0xFF, 0xFF}, 9));   // byte 1 drives the small motor, byte 2 the large one
	checkFullPacket("map rumble a second time: the reply is the previous mapping", port.packet(command(0x4D, {0x00, 0x01, 0xFF, 0xFF, 0xFF, 0xFF}, 9)),
		{0xFF, 0xF3, 0x5A, 0x00, 0x01, 0xFF, 0xFF, 0xFF, 0xFF});
	exitConfiguration(port);

	port.packet(command(0x42, {0x01, 0x80}, 5));
	check(port.pad().getSmallMotor() != 0, "rumble: poll byte 1 = 01h turns the small motor on");
	check(port.pad().getLargeMotor() == 0x80, "rumble: poll byte 2 = 80h sets the large motor to 80h", "large motor " + hex(port.pad().getLargeMotor(), 2));

	port.packet(command(0x42, {0x00, 0x00}, 5));
	check(port.pad().getSmallMotor() == 0 && port.pad().getLargeMotor() == 0, "rumble: poll bytes 00h 00h turn both motors off");
}

// Who answers, and when
void checkSelection() {
	CapturedOutput quiet;
	{
		// Dropping DTR in the middle of a packet abandons it: the next packet starts again from the beginning
		Port port;
		port.packet(command(0x42, {}, 2));   // 01h 42h, then deselect
		Packet again = port.packet(command(0x42, {}, 3));
		check(again.replies == Bytes({0xFF, 0x41, 0x5A}), "deselecting the pad mid-packet restarts the next packet", "got " + show(again.replies) + ", expected FF 41 5A");
	}
	{
		// Nothing is plugged into port 2
		Port port;
		Packet packet = port.packet(command(0x42, {}, 5), CTRL_PORT2_SELECTED);
		checkPacket("port 2 with nothing attached", packet, {0xFF, 0xFF, 0xFF, 0xFF, 0xFF}, 0);
	}
	{
		// Address 81h is a memory card, and none is attached: nobody answers
		Port port;
		Packet packet = port.packet({0x81, 0x52, 0x00, 0x00});
		checkPacket("memory card address with no card attached", packet, {0xFF, 0xFF, 0xFF, 0xFF}, 0);
	}
	{
		// DTR low: no device is selected, so nobody answers
		Port port;
		Packet packet = port.packet(command(0x42, {}, 5), CTRL_DESELECTED);
		checkPacket("pad not selected (DTR low)", packet, {0xFF, 0xFF, 0xFF, 0xFF, 0xFF}, 0);
	}
}

} // namespace

int main() {
	checkDigitalPoll();
	checkAnalogPoll();
	checkConfigurationEnterAndExit();
	checkConfigurationQueries();
	checkModeAndLock();
	checkRumbleMapping();
	checkSelection();
	return echo_check::finish();
}
