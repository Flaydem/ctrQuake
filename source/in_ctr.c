/*
Copyright (C) 2015 Felipe Izzo
Copyright (C) 1996-1997 Id Software, Inc.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/

// in_ctr.c -- for the Nintendo 3DS

#include "quakedef.h"

#include <3ds.h>
#include <sys/socket.h>
#include "ctr.h"
#include "net_udp.h"

circlePosition cstick;
circlePosition circlepad;
touchPosition oldtouch, touch;

cvar_t  csensitivity = {"csensitivity","3", true};
cvar_t  circlepadsensitivity = {"circlepadsensitivity","10.0", true};

// CS3DS: PC mouse in the emulator ---------------------------------------
// Azahar never passes mouse motion to the 3DS. scripts/souris_azahar.ps1 reads
// it on the PC and sends it over UDP (the emulated 3DS uses the PC's network):
// 5-byte packets, dx and dy as little-endian int16, then the buttons (1 = left).
// Only enabled on an emulator: Citra-based emulators answer
// svcGetSystemInfo(0x20000, 0) (Citra with 1, Azahar with 2), a real console fails.
#define NETMOUSE_PORT 27999
cvar_t  netmouse_sensitivity = {"netmouse_sensitivity","2.5", true};	// like CS (x 0.022 degree per pixel)
static int netmouse_socket = -1;
static qboolean netmouse_checked;
static int netmouse_buttons;

static void NetMouse_Init (void)
{
  s64 citra = 0;
  Result res;

  netmouse_checked = true;
  res = svcGetSystemInfo(&citra, 0x20000, 0);
  if (R_FAILED(res) || citra == 0)	// Citra answers 1, Azahar 2, a console fails
  {
    Con_Printf ("Souris PC : desactivee (console, %08lx %lld)\n", res, citra);
    return;
  }
  netmouse_socket = UDP_OpenSocket (NETMOUSE_PORT);
  if (netmouse_socket != -1)
    Con_Printf ("Souris PC : en ecoute sur le port %i\n", NETMOUSE_PORT);
  else
    Con_Printf ("Souris PC : impossible d'ouvrir le port %i\n", NETMOUSE_PORT);
}

static void NetMouse_Move (void)
{
  unsigned char packet[8];
  int dx = 0, dy = 0, buttons = netmouse_buttons;

  if (!netmouse_checked)
    NetMouse_Init ();	// here and not in IN_Init: the network must be up
  if (netmouse_socket == -1)
    return;

  while (recv (netmouse_socket, packet, sizeof(packet), 0) >= 5)
  {
    dx += (short)(packet[0] | packet[1] << 8);
    dy += (short)(packet[2] | packet[3] << 8);
    buttons = packet[4];
  }

  if ((buttons ^ netmouse_buttons) & 1)
    Key_Event (K_MOUSE1, (buttons & 1) != 0);
  if ((buttons ^ netmouse_buttons) & 2)
    Key_Event (K_MOUSE2, (buttons & 2) != 0);
  netmouse_buttons = buttons;

  if (key_dest != key_game || (!dx && !dy))
    return;
  cl.viewangles[YAW] -= dx * netmouse_sensitivity.value * 0.022;
  cl.viewangles[PITCH] += dy * netmouse_sensitivity.value * 0.022;
  V_StopPitchDrift ();
}

void IN_Init (void)
{
  Cvar_RegisterVariable (&csensitivity);
  Cvar_RegisterVariable (&circlepadsensitivity);
  Cvar_RegisterVariable (&netmouse_sensitivity);
}

void IN_Shutdown (void)
{
}

void IN_Commands (void)
{
}

extern uint8_t keyboardToggled;

#define CIRCLEPAD_DEADZONE 15
#define CIRCLEPAD_MAX 140	// full tilt reads ~146 on Azahar, a bit more or less on real consoles

// CS3DS: Circle Pad axis as -1..1, 0 at the edge of the deadzone and 1 at full tilt
static float CirclePad_Axis (int v)
{
  float f = (abs(v) - CIRCLEPAD_DEADZONE) / (float)(CIRCLEPAD_MAX - CIRCLEPAD_DEADZONE);

  if (f < 0)
    f = 0;
  if (f > 1)
    f = 1;
  return v < 0 ? -f : f;
}

void IN_Move (usercmd_t *cmd)
{

  float speed;

  if (in_speed.state & 1)
    speed = cl_movespeedkey.value;
  else
    speed = 1;

  if(hidKeysDown() & KEY_TOUCH){
    hidTouchRead(&touch);
    oldtouch = touch;
  }

  else if(hidKeysHeld() & KEY_TOUCH && !keyboardToggled){
    hidTouchRead(&touch);
    touch.px =  (touch.px + oldtouch.px) / 2;
    touch.py =  (touch.py + oldtouch.py) / 2;
    cl.viewangles[YAW] -= (touch.px - oldtouch.px) * sensitivity.value/2;
    cl.viewangles[PITCH] += (touch.py - oldtouch.py) * sensitivity.value/2;
    oldtouch = touch;
    V_StopPitchDrift ();
  }

  NetMouse_Move ();

  hidCircleRead(&circlepad);
  //CirclePad deadzone to fix ghost movements
  //CS3DS: analog movement, MOVE_FULLSCALE when the pad is pushed all the way
  if(abs(circlepad.dy) > CIRCLEPAD_DEADZONE){
    cmd->forwardmove += CirclePad_Axis(circlepad.dy) * speed * MOVE_FULLSCALE;
  }
  if(abs(circlepad.dx) > CIRCLEPAD_DEADZONE){
    float x_value = circlepad.dx / 156.0f;
    if((in_strafe.state & 1) || (lookstrafe.value))
      cmd->sidemove += CirclePad_Axis(circlepad.dx) * speed * MOVE_FULLSCALE;
    else
      cl.viewangles[YAW] -= m_side.value * x_value;
  }

  //cStick is only available on N3DS... Until libctru implements support for circlePad Pro
  if(isN3DS){

    hidCstickRead(&cstick);

    if(m_pitch.value < 0)
      cstick.dy = -cstick.dy;

    cstick.dx = abs(cstick.dx) < 10 ? 0 : cstick.dx * csensitivity.value * 0.01;
    cstick.dy = abs(cstick.dy) < 10 ? 0 : cstick.dy * csensitivity.value * 0.01;

    cl.viewangles[YAW] -= cstick.dx;
    cl.viewangles[PITCH] -= cstick.dy;
  }

  if(!lookspring.value)
    V_StopPitchDrift ();  

}
