/*
 * menu.c
 *
 *  Created on: Oct 10, 2024
 *      Author: remik
 */
#include "PCD8544_Menu.h"


/*							FUNCTIONS							*/

/**
 * @desc    Initialize menu
 *
 * @param	Pointer to first menu according to menu_description, pointer to the menu instance
 *
 * @return  Menu status
 */
Menu_Status Menu_Init(Menu_t *root, Menu_Context_t *content)
{
	content->rootMenu = root;
	content->state.CursorPosOnLCD = 0;
	content->state.PrevLCDRowPos = 0;
	content->state.MenuIndex = 0;
	content->state.PrevMenuIndex = 0;
	return Menu_OK;
}

/**
 * @desc    Refresh LCD with current settings. This function iterate throught poitners of all lined menu members.
 *
 * @param
 *
 * @return  Menu status
 */
Menu_Status Menu_GetTickFromEncoder(PCD8544_t *PCD, int8_t Position, Menu_Context_t *content)
{
	if(NULL == PCD || NULL == content)
	{
		return Menu_Error;
	}

	static int8_t Prev = 0;							//Previoue cursor position

// test czy dobrze bedzie dzialalao wchodzenie do funkcji po zmienia enkodera
	if(Position > Prev)
	{
		Menu_Next(PCD, content);
	}
	else if(Position < Prev)
	{
		Menu_Previev(PCD, content);
	}

	Prev = Position;
	return Menu_OK;
}

/**
 * @desc    Refresh LCD with current settings. This function iterate throught poitners of all lined menu members.
 *
 * @param
 *
 * @return  Menu status
 */
Menu_Status Menu_SetCursorSign(PCD8544_t *PCD, Menu_Context_t *content)
{
	if(NULL == PCD || NULL == content)
	{
		return Menu_Error;
	}

	static int8_t PrevCursorPos = 0;

    if (content->state.CursorPosOnLCD >= PCD->font.PCD8544_ROWS)			// Zabezpieczenie przed wyjściem poza ekran
    {
        content->state.CursorPosOnLCD = PCD->font.PCD8544_ROWS - 1;
    }

  // PCD8544_WriteStringToBuffer(PCD, 0, content->state.CursorPosOnLCD, ">");

    if (PrevCursorPos != content->state.CursorPosOnLCD)
    {
        PCD8544_ClearBufferRegion(PCD, 0, PrevCursorPos, 1);
        PrevCursorPos = content->state.CursorPosOnLCD;
    }

    PCD8544_UpdateScreen(PCD);
    return Menu_OK;
}

/**
 * @desc    Refresh LCD with current settings. This function iterate throught poitners of all lined menu members.
 *
 * @param
 *
 * @return  Menu status
 */
Menu_Status Menu_RefreshDisplay(PCD8544_t *PCD, Menu_Context_t *content)
{
    if (NULL == PCD || NULL == content)
	{
    	return Menu_Error;
	}

    Menu_t *tempMenu = content->rootMenu;

    // Jeśli kursor jest na górze i MenuIndex pozwala na wyświetlenie wcześniejszych elementów
    if((content->state.CursorPosOnLCD == MENU_MIN_CURSOR_ROW) && (content->state.MenuIndex >= (PCD->font.PCD8544_ROWS - 1)))
    {
    // Cofamy się o pełną wysokość ekranu aby wyświetlić poprzedni "ekran" menu
        for (uint8_t i = 0;(( i < PCD->font.PCD8544_ROWS) && (NULL != tempMenu->prev)); i++)
        {
            tempMenu = tempMenu->prev;
        }
    }
    // Jeśli kursor jest na dole i menu jest przewinięte
    else if((content->state.CursorPosOnLCD == (PCD->font.PCD8544_ROWS - 1)) && (content->state.MenuIndex >= PCD->font.PCD8544_ROWS))
    {
    // Cofamy się o (wysokość ekranu - 1) aby wyświetlić menu od odpowiedniej pozycji
        for (uint8_t i = 0; ((i < (PCD->font.PCD8544_ROWS - 1)) && (NULL != tempMenu->prev)); i++)
        {
            tempMenu = tempMenu->prev;
        }
    }
    // W pozostałych przypadkach (kursor się przesuwa)
    else
    {
    // Cofamy się tylko o tyle pozycji, na której jest kursor
        for (uint8_t i = 0; ((i < content->state.CursorPosOnLCD) && (NULL != tempMenu->prev)); i++)
        {
            tempMenu = tempMenu->prev;
        }
    }


    PCD8544_ClearBuffer(PCD);

    /*		Write data to the buffer	*/
    for (uint8_t i = 0; (i < PCD->font.PCD8544_ROWS && (NULL != tempMenu)); i++)
    {
    	PCD8544_SetCursor(PCD, 1, i);
		PCD8544_WriteString(PCD, tempMenu->name);
		tempMenu = tempMenu->next;
    }

    Menu_SetCursorSign(PCD, content);
    return Menu_OK;
}

/**
 * @desc    Select next node in linked list struct
 *
 * @param   uint8_t x, uint8_t y, - position,  char *str - pointer to string
 *
 * @return  Menu status
 */
Menu_Status Menu_Next(PCD8544_t *PCD, Menu_Context_t *content)
{
	if (NULL == PCD || NULL == content->rootMenu->next || NULL == content)
	{
		return Menu_Error;
	}

    content->rootMenu = content->rootMenu->next;
    content->state.MenuIndex++;

    /*	Change cursor positon if menu index is less that PCD8544_ROWS  [0-5]*/
    if (content->state.MenuIndex < PCD->font.PCD8544_ROWS)
    {
        content->state.CursorPosOnLCD++;
        Menu_SetCursorSign(PCD, content);			//Small optimalization
    }
    /*	Cursor stays on PCD8544_ROWS position (menu index > PCD8544_ROWS) */
    else
    {
        content->state.CursorPosOnLCD = (PCD->font.PCD8544_ROWS - 1);
        Menu_RefreshDisplay(PCD, content);			//Small optimalization,
    }
    /**
     * Menu_RefreshDisplay(PCD, content);
     * */
   	return Menu_OK;
}

/**
 * @desc    Select previous node in linked list struct
 *
 * @param   uint8_t x, uint8_t y, - position,  char *str - pointer to string
 *
 * @return  Menu status
 */
Menu_Status Menu_Previev(PCD8544_t *PCD, Menu_Context_t *content)
{
	if (NULL == PCD || NULL == content->rootMenu->prev || NULL == content)
	{
		return Menu_Error;
	}

	content->rootMenu = content->rootMenu->prev;
	content->state.MenuIndex--;

    if (content->state.CursorPosOnLCD > MENU_MIN_CURSOR_ROW)
    {
        content->state.CursorPosOnLCD--;
        Menu_SetCursorSign(PCD, content);			//Small optimalization
    }
    else
    {
    	Menu_RefreshDisplay(PCD, content);			//Small optimalization
    }

    /**
     * Menu_RefreshDisplay(PCD, content);
     * */
    return Menu_OK;
}

/**
 * @desc    Enter current menu node in linked  list struct
 *
 * @param   uint8_t x, uint8_t y, - position,  char *str - pointer to string
 *
 * @return  Menu status
 */
Menu_Status Menu_Enter(PCD8544_t *PCD, Menu_Context_t *content)
{
	if (NULL == PCD || NULL == content->rootMenu->child || NULL == content)
	{
		return Menu_Error;
	}

	if (NULL != content->rootMenu->menuFunction)
	{
		content->rootMenu->menuFunction();
	}

	content->state.PrevMenuIndex = content->state.MenuIndex;
	content->state.PrevLCDRowPos = content->state.CursorPosOnLCD;

	content->state.MenuIndex = 0;
	content->state.CursorPosOnLCD = 0;

	content->rootMenu = content->rootMenu->child;

	Menu_RefreshDisplay(PCD, content);
	return Menu_OK;
}

/**
 * @desc    Return to previous menu node in linked list
 *
 * @param   uint8_t x, uint8_t y, - position,  char *str - pointer to string
 *
 * @return  Menu status
 */
Menu_Status Menu_Escape(PCD8544_t *PCD, Menu_Context_t *content)
{
	if (NULL == PCD || NULL == content->rootMenu->parent || NULL == content)
	{
		return Menu_Error;
	}

	uint8_t tempMenuIndex;
	uint8_t tempLCDIndex;

	tempMenuIndex = content->state.MenuIndex;
	tempLCDIndex = content->state.CursorPosOnLCD;

	content->state.MenuIndex = content->state.PrevMenuIndex;
	content->state.CursorPosOnLCD = content->state.PrevLCDRowPos;

	content->state.PrevMenuIndex = tempMenuIndex;
	content->state.PrevLCDRowPos = tempLCDIndex;

	content->rootMenu = content->rootMenu->parent;

	Menu_RefreshDisplay(PCD, content);

	return Menu_OK;
}

