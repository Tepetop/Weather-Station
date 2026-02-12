/*
 * menu.h
 *
 *  Created on: Oct 10, 2024
 *      Author: remik
 */

#ifndef INC_PCD8544_MENU_H_
#define INC_PCD8544_MENU_H_
#include <PCD8544.h>
#include "main.h"

/*		DEFINES										*/

#define MENU_MIN_CURSOR_ROW			0x00U
#define MENU_MAX_DEPTH              5       // Maksymalna głębokość zagnieżdżenia

/*		Struktura list połączonych menu					*/
typedef struct menu_s Menu_t;
struct menu_s
{
	const char *name;
	const char *details;
	Menu_t *next;
	Menu_t *prev;
	Menu_t *child;
	Menu_t *parent;
	void(*menuFunction)(void);
};

/*		Menu Action enumeration for state machine			*/
typedef enum
{
	MENU_ACTION_IDLE = 0x00U,
	MENU_ACTION_NEXT,
	MENU_ACTION_PREV,
	MENU_ACTION_ENTER,
	MENU_ACTION_ESCAPE
}Menu_Action_t;

/*		Zmienne potrzebne do obsługi biblioteki menu											*/

typedef struct
{
    uint8_t		MenuIndex;
    uint8_t 	CursorPosOnLCD;
    // Zmieniono na tablice (stos)
    uint8_t		PrevMenuIndex[MENU_MAX_DEPTH];
    uint8_t		PrevLCDRowPos[MENU_MAX_DEPTH];
    uint8_t     CurrentDepth;
    // State machine
    Menu_Action_t currentAction;
    uint8_t actionPending;
}Menu_Variables_t;

/*		Struktura zawierająca wskaźnik na pierwszy element zdefiniowanego menu oraz strukturę zmiennych menu	*/

typedef struct
{
	Menu_t				*rootMenu;
	Menu_Variables_t	state;
}Menu_Context_t;

/*		Struktura statusu menu	TODO: Potrzebna rozbudowa i lepsze opisy stanów						*/

typedef enum
{
	Menu_OK	= 0x00U,
	Menu_Error
}Menu_Status;


/*		Functions prototypes		*/
Menu_Status Menu_Init(Menu_t *root, Menu_Context_t *content);
Menu_Status Menu_GetTickFromEncoder(PCD8544_t *PCD, int8_t Position, Menu_Context_t *content);
Menu_Status Menu_SetCursorSign(PCD8544_t *PCD, Menu_Context_t *content);
Menu_Status Menu_RefreshDisplay(PCD8544_t *PCD, Menu_Context_t *content);
Menu_Status Menu_Next(PCD8544_t *PCD, Menu_Context_t *content);
Menu_Status Menu_Previev(PCD8544_t *PCD, Menu_Context_t *content);
Menu_Status Menu_Enter(PCD8544_t *PCD, Menu_Context_t *content);
Menu_Status Menu_Escape(PCD8544_t *PCD, Menu_Context_t *content);

/*		State Machine functions		*/
Menu_Status Menu_Task(PCD8544_t *PCD, Menu_Context_t *content);
void Menu_SetAction(Menu_Context_t *content, Menu_Action_t action);
void Menu_SetNextAction(Menu_Context_t *content);
void Menu_SetPrevAction(Menu_Context_t *content);
void Menu_SetEnterAction(Menu_Context_t *content);
void Menu_SetEscapeAction(Menu_Context_t *content);

#endif /* INC_MENU_H_ */
