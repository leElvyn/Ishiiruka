// Copyright (C) 2003-2009 Dolphin Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official SVN repository and contact information can be found at
// http://code.google.com/p/dolphin-emu/

// =====================================================================================================
// GameBoy Advance
// =====================================================================================================

class CSIDevice_GBA : public ISIDevice
{
private:
	
	// commands
	enum EBufferCommands
	{
		CMD_RESET		= 0xFF,
		CMD_STATUS		= 0x00,
		CMD_WRITE		= 0x15,
		CMD_READ		= 0x14
	};

	//0x4000158 - JOYSTAT - Receive Status Register (R/W) (ON THE GBA)
	//Bit   Explanation
	//0     Not used
	//1     Receive Status Flag   (0=Remote GBA is/was receiving) (Read Only?)
	//2     Not used
	//3     Send Status Flag      (1=Remote GBA is/was sending)   (Read Only?)
	//4-5   General Purpose Flag  (Not assigned, may be used for whatever purpose)
	//6-15  Not used
	//--------------------------------------
	//Bit 1 is automatically set when writing to local JOY_TRANS.
	//Bit 3 is automatically reset when reading from local JOY_RECV.

	int DeviceNum;

public:

	// constructor
	CSIDevice_GBA(int _iDeviceNumber);

	// run the SI Buffer
	virtual int RunBuffer(u8* _pBuffer, int _iLength);

	// return true on new data
	virtual bool GetData(u32& _Hi, u32& _Low);

	// send a command directly
	virtual void SendCommand(u32 _Cmd);
};
