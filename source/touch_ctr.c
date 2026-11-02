/*
Copyright (C) 2015 Felipe Izzo

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

//FIX ME: load all hardcoded values from file

#include "quakedef.h"

#include <3ds.h>
#include "touch_ctr.h"

//Keyboard is currently laid out on a 14*4 grid of 20px*20px boxes for lazy implementation
char keymap[14 * 6] = {
  K_ESCAPE , K_F1, K_F2, K_F3, K_F4, K_F5, K_F6, K_F7, K_F8, K_F9, K_F10, K_F11, K_F12, 0,
  '`' , '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', K_BACKSPACE,
  K_TAB, 'q' , 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '|',
  0, 'a' , 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', K_ENTER, K_ENTER,
  K_SHIFT, 'z' , 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0, K_UPARROW, 0,
  0, 0 , 0, 0, K_SPACE, K_SPACE, K_SPACE, K_SPACE, K_SPACE, K_SPACE, 0, K_LEFTARROW, 	K_DOWNARROW, K_RIGHTARROW
};

u16* touchOverlay;
u16* keyboardOverlay;
uint8_t keyboardToggled;
char lastKey = 0;
int tmode;
u16* tfb;
touchPosition oldtouch, touch;
u64 tick;

u64 lastTap = 0;

int shiftToggle = 0;

// CS3DS: menu d'achat, voir a la fin du fichier
#define BUY_WIDTH_TAP 264
extern qboolean buymenu_open;
static void BuyMenu_Update (void);
static void BuyMenu_Tap (void);

void Touch_Init(){
  tmode = TMODE_TOUCHPAD; //Start in touchpad Mode
  tfb = (u16*)gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, NULL, NULL);

  //Load overlay files from sdmc for easier testing
  FILE *texture = fopen("touchOverlay.bin", "rb");
  if(!texture)
    Sys_Error("Could not open touchpadOverlay.bin\n");
  fseek(texture, 0, SEEK_END);
  int size = ftell(texture);
  fseek(texture, 0, SEEK_SET);
  touchOverlay = malloc(size);
  fread(touchOverlay, 1, size, texture);
  fclose(texture);

  texture = fopen("keyboardOverlay.bin", "rb");
  if(!texture)
    Sys_Error("Could not open keyboardOverlay.bin\n");
  fseek(texture, 0, SEEK_END);
  size = ftell(texture);
  fseek(texture, 0, SEEK_SET);
  keyboardOverlay = malloc(size);
  fread(keyboardOverlay, 1, size, texture);
  fclose(texture);
}

void Touch_DrawOverlay()
{
  int x, y;
  for(x=0; x<320; x++){
    for(y=0; y<240;y++){
      tfb[(x*240 + (239 - y))] = touchOverlay[(y*320 + x)];
    }
  }
  if(keyboardToggled)
    Touch_DrawKeyboard();
}

void Touch_DrawKeyboard()
{
  int x, y;
  for(x=0; x<320; x++){
    for(y=0; y<240;y++){
      tfb[(x*240 + (239 - y))] = keyboardOverlay[(y*320 + x)];
    }
  }
  if(shiftToggle)
  {
    for(x=26; x<29; x++){
      for(y=149; y<152;y++){
        tfb[((x)*240 + (239 - (y)))] = RGB8_to_565(0,255,0);
      }
    }
  }
}

void Touch_Update(){
  if(lastKey){
    Key_Event(lastKey, false);
    lastKey = 0;
  }

  if(hidKeysDown() & KEY_TOUCH){
    hidTouchRead(&touch);
    tick = Sys_FloatTime();
  }

  //CS3DS: only a short press is a tap, so that releasing the stylus
  //(or the mouse in Azahar) after aiming doesn't press a button
  if(hidKeysUp() & KEY_TOUCH && Sys_FloatTime() - tick < 0.3){
      Touch_ProcessTap();
  }
  BuyMenu_Update();
}

void Touch_KeyboardToggle()
{
  if(keyboardToggled)
    shiftToggle = 0;
    Key_Event(K_SHIFT,false);

  keyboardToggled = !keyboardToggled;

  Touch_DrawOverlay();
}

void Touch_ProcessTap()
{
  if(buymenu_open && touch.px < BUY_WIDTH_TAP){
    BuyMenu_Tap();
    return;
  }
  if(touch.px > 268 && touch.py > 14 && touch.py < 226 && !keyboardToggled)
    Touch_SideBarTap();
  else if (touch.py > 62 && touch.py < 188 && touch.px > 12 && touch.px < 308 && keyboardToggled)
    Touch_KeyboardTap();
  else if (touch.py > 214 && touch.px > 142 && touch.px < 178)
    Touch_KeyboardToggle();
}

void Touch_SideBarTap()
{
  uint16_t y = (touch.py - 14)/42;
  lastKey = K_AUX9 + y;
  Key_Event(lastKey, true);
}

void Touch_KeyboardTap()
{
  char key = keymap[((touch.py - 62)/21) * 14 + (touch.px - 12)/21];
  if(key == K_SHIFT){
    shiftToggle = !shiftToggle;
    Key_Event(K_SHIFT,shiftToggle);
    Touch_DrawOverlay();
  }
  else {
    Key_Event(key, true);
    lastKey = key;
  }
}
/*
==============
CS3DS : menu d'achat (game/buy.qc)

Ouvert et ferme par la commande "achat" (touche T4). Dessine sur l'ecran du
bas, a gauche des touches T1 a T5, avec la police de Quake. Un appui sur un
article envoie "impulse N" au serveur, qui verifie (argent, equipe, zone et
temps d'achat) et repond a l'ecran. Le serveur envoie dans cl.items : equipe
CT, achat possible, casque, kit (game/match.qc, MatchPlayerFrame).
Les prix sont recopies de game/weapon.qc et game/buy.qc.
==============
*/
typedef struct
{
  char *name;
  int price;
  int team;		// 0 : les deux equipes, 1 : T, 2 : CT
  int impulse;
} buyitem_t;

static buyitem_t buyitems[] = {
  {"AK-47", 2700, 1, 41},
  {"M4A4", 3100, 2, 42},
  {"AWP", 4750, 0, 43},
  {"MAC-10", 1050, 1, 44},
  {"GILET", 650, 0, 50},
  {"GILET + CASQUE", 1000, 0, 51},
  {"KIT DE DESAMORCAGE", 400, 2, 52},
};
#define BUY_ITEMS	(sizeof(buyitems) / sizeof(buyitems[0]))
#define BUY_WIDTH	264		// a droite : les touches T1 a T5
#define BUY_TOP		32		// sous le titre
#define BUY_ROW		41		// hauteur d'une ligne (bouton + marge)
#define BUY_BUTTON	37

qboolean buymenu_open;
static int buy_list[BUY_ITEMS], buy_count;
static int buy_money = -1, buy_items, buy_armor;	// etat affiche (redessine s'il change)

static void Buy_Fill (int x0, int y0, int w, int h, u16 color)
{
  int x, y;
  for (x = x0; x < x0 + w; x++)
    for (y = y0; y < y0 + h; y++)
      tfb[x*240 + (239 - y)] = color;
}

// texte avec la police de Quake (8x8, agrandie scale fois)
static void Buy_Text (int x, int y, const char *s, u16 color, int scale)
{
  extern byte *draw_chars;
  int i, j, c;
  byte *src;

  for ( ; *s ; s++, x += 8 * scale)
  {
    c = *s & 255;
    src = draw_chars + ((c >> 4) << 10) + ((c & 15) << 3);
    for (j = 0; j < 8; j++, src += 128)
      for (i = 0; i < 8; i++)
        if (src[i])
          Buy_Fill (x + i * scale, y + j * scale, scale, scale, color);
  }
}

static int Buy_Price (buyitem_t *it)
{
  // casque seul : 350 $ si le gilet est intact (game/buy.qc)
  if (it->impulse == 51 && cl.stats[STAT_ARMOR] >= 100 && !(cl.items & IT_ARMOR2))
    return 350;
  return it->price;
}

static void BuyMenu_Draw (void)
{
  int i, y, team, price, can;
  buyitem_t *it;
  char str[32];
  u16 name, cost;

  buy_money = cl.stats[STAT_MONEY];
  buy_items = cl.items;
  buy_armor = cl.stats[STAT_ARMOR];
  team = (cl.items & IT_INVULNERABILITY) ? 2 : 1;
  can = (cl.items & IT_INVISIBILITY) != 0;

  // articles de son equipe
  buy_count = 0;
  for (i = 0; i < BUY_ITEMS; i++)
    if (!buyitems[i].team || buyitems[i].team == team)
      buy_list[buy_count++] = i;

  Buy_Fill (0, 0, BUY_WIDTH, 240, RGB8_to_565(24, 20, 16));
  if (can)
    Buy_Text (8, 8, "ACHAT", RGB8_to_565(230, 190, 80), 2);
  else
    Buy_Text (8, 12, "ACHAT IMPOSSIBLE", RGB8_to_565(220, 70, 60), 1);
  sprintf (str, "%i $", buy_money);
  Buy_Text (BUY_WIDTH - 8 - strlen(str) * 16, 8, str, RGB8_to_565(230, 190, 80), 2);

  for (i = 0; i < buy_count; i++)
  {
    it = &buyitems[buy_list[i]];
    price = Buy_Price (it);
    y = BUY_TOP + i * BUY_ROW;
    if (can && buy_money >= price)
    {
      name = RGB8_to_565(255, 255, 255);
      cost = RGB8_to_565(230, 190, 80);
    }
    else
    {
      name = RGB8_to_565(110, 105, 100);
      cost = RGB8_to_565(170, 60, 50);
    }
    Buy_Fill (6, y, BUY_WIDTH - 12, BUY_BUTTON, RGB8_to_565(60, 50, 38));
    Buy_Text (14, y + 4, it->name, name, strlen(it->name) > 14 ? 1 : 2);
    sprintf (str, "%i $", price);
    Buy_Text (14, y + 24, str, cost, 1);
  }
}

static void BuyMenu_Toggle_f (void)
{
  buymenu_open = !buymenu_open;
  if (buymenu_open)
    BuyMenu_Draw ();
  else
    Touch_DrawOverlay ();
}

void BuyMenu_Init (void)
{
  Cmd_AddCommand ("achat", BuyMenu_Toggle_f);
}

// a chaque image : ferme le menu quand l'achat n'est plus possible, le redessine
// quand l'argent ou l'equipement change
static void BuyMenu_Update (void)
{
  if (!buymenu_open)
    return;
  if (cls.state != ca_connected || ((buy_items & IT_INVISIBILITY) && !(cl.items & IT_INVISIBILITY)))
  {
    buymenu_open = false;
    Touch_DrawOverlay ();
    return;
  }
  if (cl.stats[STAT_MONEY] != buy_money || cl.items != buy_items || cl.stats[STAT_ARMOR] != buy_armor)
    BuyMenu_Draw ();
}

static void BuyMenu_Tap (void)
{
  int row = (touch.py - BUY_TOP) / BUY_ROW;

  if (touch.py < BUY_TOP || row >= buy_count || (touch.py - BUY_TOP) % BUY_ROW >= BUY_BUTTON)
    return;
  Cbuf_AddText (va ("impulse %i\n", buyitems[buy_list[row]].impulse));
}
