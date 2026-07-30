/**
 * @file PCD8544_Menu.c
 * @brief Linked-list menu navigation implementation for the PCD8544 display.
 * @details Menu tree traversal, encoder/button action handling, cursor rendering,
 *          and framebuffer refresh for the indoor unit UI.
 */

#include "PCD8544_Menu.h"
#include "PCD8544.h"
#include "PCD8544_config.h"


/*							FUNCTIONS							*/

/**
 * @brief Initializes menu context with the root menu tree.
 * @param[in]  root    Root menu node (first top-level item).
 * @param[out] content Menu context to initialize.
 * @retval Menu_OK    Context initialized successfully.
 * @retval Menu_Error Invalid root or content pointer.
 */
Menu_Status Menu_Init(Menu_t *root, Menu_Context_t *content)
{
	content->rootMenu = root;
    content->defaultMenu = root;
	content->state.CursorPosOnLCD = 0;
	content->state.MenuIndex = 0;
	content->state.CurrentDepth = 0;
	content->state.InDetailsView = 0;
    content->state.InDefaultMeasurementsView = 1;   // Default to showing measurements on startup
    content->state.InStationsStatusView = 0;
	
	// Initialize arrays to zero
	for(uint8_t i = 0; i < MENU_MAX_DEPTH; i++)
	{
		content->state.PrevLCDRowPos[i] = 0;
		content->state.PrevMenuIndex[i] = 0;
	}
	
	// Initialize state machine to IDLE
	content->state.currentAction = MENU_ACTION_IDLE;
	content->state.actionPending = 0;
	
	return Menu_OK;
}

/**
 * @brief Updates inverted cursor highlight for the current selection.
 * @param[in,out] PCD     Display driver instance.
 * @param[in,out] content Menu context with current cursor position.
 * @retval Menu_OK    Cursor sign updated successfully.
 * @retval Menu_Error Invalid pointer.
 */
Menu_Status Menu_SetCursorSign(PCD8544_t *PCD, Menu_Context_t *content)
{
	if(NULL == PCD || NULL == content)
	{
		return Menu_Error;
	}

	static int8_t PrevCursorPos = 0;
    
    // Determine layout based on Depth and Config
    // If Depth > 0 (Submenu), we reserve Row 0 for Title, so items start at Row 1
    // If Depth == 0 (Main Menu), we use full screen (Row 0..Items)
    
    uint8_t y_offset = 0;
    uint8_t viewportHeight = PCD->font.PCD8544_ROWS;

    if (content->state.CurrentDepth > 0) {
        y_offset = 1;
        viewportHeight = PCD->font.PCD8544_ROWS - 1;
    }

    // Cursor Index runs from 0 to (viewportHeight - 1). 
    // e.g. Viewport=5 -> Index 0..4.
    uint8_t max_cursor_index = viewportHeight - 1;

    if (content->state.CursorPosOnLCD > max_cursor_index)
    {
        content->state.CursorPosOnLCD = max_cursor_index;
    }

    if (PrevCursorPos != content->state.CursorPosOnLCD)
    {
        PCD8544_ClearBufferRegion(PCD, 0, PrevCursorPos + y_offset, 1);
        PrevCursorPos = content->state.CursorPosOnLCD;
    }
    
    // ODKOMENTUJ TĘ LINIĘ:
    PCD8544_SetCursor(PCD, 0, content->state.CursorPosOnLCD + y_offset);
    PCD8544_WriteChar(PCD, ">");
    PCD8544_UpdateScreen(PCD);
    return Menu_OK;
}

/**
 * @brief Renders the current menu level into the display framebuffer.
 * @param[in,out] PCD     Display driver instance.
 * @param[in,out] content Menu context with active menu branch.
 * @retval Menu_OK    Menu drawn to buffer successfully.
 * @retval Menu_Error Invalid pointer.
 */
Menu_Status Menu_RefreshDisplay(PCD8544_t *PCD, Menu_Context_t *content)
{
    if (NULL == PCD || NULL == content)
	{
    	return Menu_Error;
	}

    PCD8544_ClearBuffer(PCD);
    Menu_t *tempMenu = content->rootMenu;
    uint8_t effectiveDepth = content->state.CurrentDepth;

#ifdef PCD8544_SHOW_DETAILS
    // --- Details View Mode ---
    if(content->state.InDetailsView)
    {
        // 1. Draw Title (Current Item Name)
        PCD8544_SetCursor(PCD, 0, 0); 
        const char* titleString = tempMenu->name; // In Details View, rootMenu is the item itself
        
        // Centering logic
        uint8_t textPixelWidth = (strlen(titleString) + 2) * PCD->font.font_width;
        if(textPixelWidth < PCD8544_WIDTH)
        {
             PCD->buffer.PCD8544_CurrentX = (PCD8544_WIDTH - textPixelWidth) / 2;
        }
        PCD8544_WriteString(PCD, "-");
        PCD8544_WriteString(PCD, (char*)titleString);
        PCD8544_WriteString(PCD, "-");

        // 2. Draw "Return"
        PCD8544_SetCursor(PCD, 1, 1);
        PCD8544_WriteString(PCD, "Return");

        // 3. Draw Details
        if(tempMenu->details != NULL)
        {
            PCD8544_SetCursor(PCD, 0, 2);
            PCD8544_WriteString(PCD, tempMenu->details);
        }
        
        PCD8544_UpdateScreen(PCD);
        return Menu_OK;
    }
#endif

    // --- Title Logic (Row 0) ---
    // Show title ONLY if Depth > 0 (Submenu)
    uint8_t listStartVisualRow = 0;
    uint8_t viewportHeight = PCD->font.PCD8544_ROWS;

    if (effectiveDepth > 0)
    {
        PCD8544_SetCursor(PCD, 0, 0); 
        const char* titleString = "SUBMENU";
        if (tempMenu != NULL && tempMenu->parent != NULL) {
            titleString = tempMenu->parent->name;
        }
        
        // Calculate centering: (Screen_Width - Text_Width) / 2
        // Text is "-TITLE-"
        PCD_8544_DrawCenteredTitle(PCD, titleString);

        listStartVisualRow = 1;
        viewportHeight = PCD->font.PCD8544_ROWS - 1;
    }

#if PCD8544_ENCODER_MODE
    int16_t startIndex;

    // Calculate start index for display
    startIndex = (int16_t)content->state.MenuIndex - (int16_t)content->state.CursorPosOnLCD;
    if (startIndex < 0) startIndex = 0;

    // Navigate to the correct menu item for display start
    int16_t anchorVirtualIndex = 0; // Default for Main Menu
    if (effectiveDepth > 0) {
        anchorVirtualIndex = (content->state.MenuIndex == 0) ? 1 : content->state.MenuIndex;
    } else {
        anchorVirtualIndex = content->state.MenuIndex;
    }

    // Adjust neededVirtualIndex based on whether we have "POWROT" or not
    // In Encoder Mode:
    // If Depth > 0: Index 0 is POWROT. Items start at 1.
    // If Depth == 0: Index 0 is Item 0.
    int16_t neededVirtualIndex = startIndex;
    if (effectiveDepth > 0 && startIndex == 0) neededVirtualIndex = 1; // If showing top of submenu, align to first real item
    
    // Calculate offset
    int16_t offset;
    if (effectiveDepth > 0) {
       offset = neededVirtualIndex - anchorVirtualIndex;
    } else {
       offset = startIndex - (int16_t)content->state.MenuIndex;
    }
   
    while (offset > 0 && tempMenu != NULL) { tempMenu = tempMenu->next; offset--; }
    while (offset < 0 && tempMenu != NULL) { tempMenu = tempMenu->prev; offset++; }

    for (uint8_t i = 0; i < viewportHeight; i++)
    {
        int16_t currentVirtualIndex = startIndex + i;
        uint8_t visualRow = listStartVisualRow + i;

        PCD8544_SetCursor(PCD, 1, visualRow);
        
        if (effectiveDepth > 0 && currentVirtualIndex == 0) {
            PCD8544_WriteString(PCD, "Return");
        } else {
            if (tempMenu != NULL) {
                PCD8544_WriteString(PCD, tempMenu->name);
                tempMenu = tempMenu->next;
            }
        }
    }
#else
    // --- Normal Mode (Linked List Traversal Logic) ---
    // Logic from original code, adapted for viewport
    
    // Jeśli kursor jest na górze i MenuIndex pozwala na wyświetlenie wcześniejszych elementów
    if((content->state.CursorPosOnLCD == MENU_MIN_CURSOR_ROW) && (content->state.MenuIndex >= (viewportHeight - 1)))
    {
        // Cofamy się o pełną wysokość ekranu viewport
        for (uint8_t i = 0;(( i < viewportHeight) && (NULL != tempMenu->prev)); i++)
        {
            tempMenu = tempMenu->prev;
        }
    }
    // Jeśli kursor jest na dole i menu jest przewinięte
    else if((content->state.CursorPosOnLCD == (viewportHeight - 1)) && (content->state.MenuIndex >= viewportHeight))
    {
        // Cofamy się o (viewport - 1)
        for (uint8_t i = 0; ((i < (viewportHeight - 1)) && (NULL != tempMenu->prev)); i++)
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

    /*		Write data to the buffer	*/
    for (uint8_t i = 0; (i < viewportHeight && (NULL != tempMenu)); i++)
    {
    	PCD8544_SetCursor(PCD, 1, listStartVisualRow + i); // Correct visual row with offset
		PCD8544_WriteString(PCD, tempMenu->name);
		tempMenu = tempMenu->next;
    }
#endif

    Menu_SetCursorSign(PCD, content);
    return Menu_OK;
}

/**
 * @brief Moves selection to the next menu item and refreshes the display.
 * @param[in,out] PCD     Display driver instance.
 * @param[in,out] content Menu context to advance.
 * @retval Menu_OK    Selection moved or blocked at end of list.
 * @retval Menu_Error Invalid pointer or no next item available.
 */
Menu_Status Menu_Next(PCD8544_t *PCD, Menu_Context_t *content)
{
    if (NULL == PCD || NULL == content) return Menu_Error;
    
#ifdef PCD8544_SHOW_DETAILS
    // In Details View, we have only one option (Return at index 0), so block Next
    if(content->state.InDetailsView) return Menu_OK;
#endif

#if PCD8544_ENCODER_MODE
    // Special handling for moving FROM "POWROT" (index 0) TO the first item (index 1)
    if (content->state.CurrentDepth > 0 && content->state.MenuIndex == 0) {
        if (content->rootMenu != NULL) {
             content->state.MenuIndex++;
             content->state.CursorPosOnLCD++;
             Menu_SetCursorSign(PCD, content);
             return Menu_OK;
        }
        return Menu_Error;
    }

    // Normal navigation checks
    if (content->rootMenu == NULL || content->rootMenu->next == NULL) return Menu_Error;
#else
	if (NULL == content->rootMenu->next)
	{
		return Menu_Error;
	}
#endif

    content->rootMenu = content->rootMenu->next;
    content->state.MenuIndex++;

    #if PCD8544_ENCODER_MODE
        /* Window size is reduced by 1 (Title Row). Max Cursor Index is ROWS - 2 (e.g. 4) */
        /* If MenuIndex fits in initial window (0 to ROWS-2), just inc cursor */
        /* e.g. ROWS=6. Max Item Visible at start = 4. If Index becomes 5, we scroll. */
        if (content->state.MenuIndex < (PCD->font.PCD8544_ROWS - 1))
        {
            content->state.CursorPosOnLCD++;
        }
        else
        {
            content->state.CursorPosOnLCD = (PCD->font.PCD8544_ROWS - 2);
        }
    #else
        /*  Determine viewport height: full screen if Main Menu (depth 0), -1 if Submenu (depth > 0) */
        uint8_t viewportHeight = PCD->font.PCD8544_ROWS;
        if (content->state.CurrentDepth > 0) viewportHeight--;

        /*	Change cursor positon if menu index is less that viewPort */
        if (content->state.MenuIndex < viewportHeight)
        {
            content->state.CursorPosOnLCD++;
        }
        /*	Cursor stays on viewPort position (menu index > viewPort) */
        else
        {
            content->state.CursorPosOnLCD = (viewportHeight - 1);
        }
    #endif

    Menu_RefreshDisplay(PCD, content);
    /**
     * Menu_RefreshDisplay(PCD, content);
     * */
   	return Menu_OK;
}

/**
 * @brief Moves selection to the previous menu item and refreshes the display.
 * @param[in,out] PCD     Display driver instance.
 * @param[in,out] content Menu context to move backward.
 * @retval Menu_OK    Selection moved or blocked at start of list.
 * @retval Menu_Error Invalid pointer or no previous item available.
 */
Menu_Status Menu_Previev(PCD8544_t *PCD, Menu_Context_t *content)
{
    if (NULL == PCD || NULL == content) return Menu_Error;
    
#ifdef PCD8544_SHOW_DETAILS
    // In Details View, we have only one option (Return at index 0), so block Prev (scrolling up)
    if(content->state.InDetailsView) return Menu_OK;
#endif

#if PCD8544_ENCODER_MODE
    // Handling transition FROM first item (index 1) TO "POWROT" (index 0)
    if (content->state.CurrentDepth > 0 && content->state.MenuIndex == 1) {
        content->state.MenuIndex--;
        if (content->state.CursorPosOnLCD > 0) {
            content->state.CursorPosOnLCD--;
            Menu_SetCursorSign(PCD, content);
        } else {
            // Should not happen if logic matches display, but refresh safe
            Menu_RefreshDisplay(PCD, content);
        }
        return Menu_OK;
    }
    
    // Already at top (RETURN option)
    if (content->state.CurrentDepth > 0 && content->state.MenuIndex == 0) {
        return Menu_OK; 
    }

    if (content->rootMenu == NULL || content->rootMenu->prev == NULL) return Menu_Error;
#else
	if (NULL == content->rootMenu->prev)
	{
		return Menu_Error;
	}
#endif

	content->rootMenu = content->rootMenu->prev;
	content->state.MenuIndex--;

    if (content->state.CursorPosOnLCD > MENU_MIN_CURSOR_ROW)
    {
        content->state.CursorPosOnLCD--;
    }

	Menu_RefreshDisplay(PCD, content);

    /**
     * Menu_RefreshDisplay(PCD, content);
     * */
    return Menu_OK;
}

/**
 * @brief Enters a submenu, executes a leaf callback, or opens a details view.
 * @param[in,out] PCD     Display driver instance.
 * @param[in,out] content Menu context for the current selection.
 * @retval Menu_OK    Enter action handled successfully.
 * @retval Menu_Error Invalid pointer, depth limit, or missing child.
 */
Menu_Status Menu_Enter(PCD8544_t *PCD, Menu_Context_t *content)
{
    if (NULL == PCD || NULL == content) return Menu_Error;

#if PCD8544_ENCODER_MODE
    // Returning from submenu if RETURN selected
    if (content->state.CurrentDepth > 0 && content->state.MenuIndex == 0) {
        return Menu_Escape(PCD, content);
    }
#endif

    if (NULL == content->rootMenu->child)
	{
        if (NULL != content->rootMenu->menuFunction)
        {
            content->rootMenu->menuFunction();
            if (content->rootMenu == content->defaultMenu)
            {
                content->state.InDefaultMeasurementsView = 1;
            }
            return Menu_OK;
        }

#ifdef PCD8544_SHOW_DETAILS
		// Check for details
        if(content->rootMenu->details != NULL)
        {
            // Enter Details Mode
            if (content->state.CurrentDepth >= MENU_MAX_DEPTH) return Menu_Error;

            // Save state
            content->state.PrevMenuIndex[content->state.CurrentDepth] = content->state.MenuIndex;
            content->state.PrevLCDRowPos[content->state.CurrentDepth] = content->state.CursorPosOnLCD;

            content->state.CurrentDepth++;
            content->state.InDetailsView = 1;
            
            // Set context for new view (Only return option)
            content->state.MenuIndex = 0;
            content->state.CursorPosOnLCD = 0;
            
            // Do NOT change rootMenu
            
            Menu_RefreshDisplay(PCD, content);
            // Also need to draw the cursor (at pos 0, offset 1 due to depth > 0)
            Menu_SetCursorSign(PCD, content);
            
            return Menu_OK;
        }
#endif

		return Menu_OK; 
	}

	if (NULL != content->rootMenu->menuFunction)
	{
		content->rootMenu->menuFunction();
	}

	// Zabezpieczenie przed przepełnieniem stosu
    if (content->state.CurrentDepth >= MENU_MAX_DEPTH) return Menu_Error;

    // Zapisz obecną pozycję na stosie na obecnym poziomie głębokości
    content->state.PrevMenuIndex[content->state.CurrentDepth] = content->state.MenuIndex;
    content->state.PrevLCDRowPos[content->state.CurrentDepth] = content->state.CursorPosOnLCD;
    
    // Zwiększ głębokość
    content->state.CurrentDepth++;

    content->state.MenuIndex = 0;
    content->state.CursorPosOnLCD = 0;

    content->rootMenu = content->rootMenu->child;

    Menu_RefreshDisplay(PCD, content);
    return Menu_OK;
}

/**
 * @brief Returns to the parent menu or exits a special overlay view.
 * @param[in,out] PCD     Display driver instance.
 * @param[in,out] content Menu context to restore from navigation stack.
 * @retval Menu_OK    Escape handled successfully.
 * @retval Menu_Error Invalid pointer or stack underflow.
 */
Menu_Status Menu_Escape(PCD8544_t *PCD, Menu_Context_t *content)
{
    if(NULL == PCD || NULL == content) return Menu_Error;

#ifdef PCD8544_SHOW_DETAILS
    // Handle Exit from Details View
    if(content->state.InDetailsView)
    {
        if(content->state.CurrentDepth > 0)
        {
            content->state.CurrentDepth--;
            content->state.InDetailsView = 0;
            
            // Restore State
            content->state.MenuIndex = content->state.PrevMenuIndex[content->state.CurrentDepth];
            content->state.CursorPosOnLCD = content->state.PrevLCDRowPos[content->state.CurrentDepth];
            
            // Do NOT change rootMenu (we never left it)
            
            Menu_RefreshDisplay(PCD, content);
            
            return Menu_OK;
        }
        return Menu_Error;
    }
#endif

    // Jeśli to główne menu (parent NULL) lub głębokość 0, nie można wyjść
    if (NULL == content->rootMenu->parent || content->state.CurrentDepth == 0)
    {
        return Menu_Error;
    }

    // Zmniejsz głębokość, aby wrócić do poprzedniego poziomu
    content->state.CurrentDepth--;

    // Przywróć pozycję ze stosu
    content->state.MenuIndex = content->state.PrevMenuIndex[content->state.CurrentDepth];
    content->state.CursorPosOnLCD = content->state.PrevLCDRowPos[content->state.CurrentDepth];

    content->rootMenu = content->rootMenu->parent;

    Menu_RefreshDisplay(PCD, content);

    return Menu_OK;
}

/*							STATE MACHINE FUNCTIONS							*/

/**
 * @brief Processes one pending menu action from the navigation state machine.
 * @param[in,out] PCD     Display driver instance.
 * @param[in,out] content Menu context with actionPending flag.
 * @retval Menu_OK    Task step completed (action cleared when handled).
 * @retval Menu_Error Invalid pointer.
 */
Menu_Status Menu_Task(PCD8544_t *PCD, Menu_Context_t *content)
{
    if(NULL == PCD || NULL == content)
    {
        return Menu_Error;
    }
    
    // Handle special default measurements view mode
    if(content->state.InDefaultMeasurementsView)
    {
        if(content->state.actionPending)
        {
            if(content->state.currentAction == MENU_ACTION_ENTER)
            {
                content->state.InDefaultMeasurementsView = 0;

                if(content->defaultMenu != NULL && content->defaultMenu->next != NULL)
                {
                    content->rootMenu = content->defaultMenu->next;
                    content->state.MenuIndex = 1;
                    content->state.CursorPosOnLCD = 1;
                    Menu_RefreshDisplay(PCD, content);
                }
            }

            content->state.actionPending = 0;
            content->state.currentAction = MENU_ACTION_IDLE;
        }

        return Menu_OK;
    }

    // Check if there's a pending action
    if(content->state.actionPending)
    {
        switch(content->state.currentAction)
        {
            case MENU_ACTION_NEXT:
                Menu_Next(PCD, content);
                break;
                
            case MENU_ACTION_PREV:
                Menu_Previev(PCD, content);
                break;
                
            case MENU_ACTION_ENTER:
                Menu_Enter(PCD, content);
                break;
                
            case MENU_ACTION_ESCAPE:
                Menu_Escape(PCD, content);
                break;
                
            case MENU_ACTION_IDLE:
            default:
                // No action or invalid action
                break;
        }
        
        // Clear the pending action
        content->state.actionPending = 0;
        content->state.currentAction = MENU_ACTION_IDLE;
    }
    
    return Menu_OK;
}

/**
 * @brief Queues a generic menu navigation action (safe from interrupt context).
 * @param[in,out] content Menu context to update.
 * @param[in]     action  Navigation action to queue.
 */
void Menu_SetAction(Menu_Context_t *content, Menu_Action_t action)
{
    if(content != NULL)
    {
        content->state.currentAction = action;
        content->state.actionPending = 1;
    }
}

/**
 * @brief Queues MENU_ACTION_NEXT (typically from encoder or button ISR).
 * @param[in,out] content Menu context to update.
 */
void Menu_SetNextAction(Menu_Context_t *content)
{
    Menu_SetAction(content, MENU_ACTION_NEXT);
}

/**
 * @brief Queues MENU_ACTION_PREV (typically from encoder or button ISR).
 * @param[in,out] content Menu context to update.
 */
void Menu_SetPrevAction(Menu_Context_t *content)
{
    Menu_SetAction(content, MENU_ACTION_PREV);
}

/**
 * @brief Queues MENU_ACTION_ENTER (typically from select button ISR).
 * @param[in,out] content Menu context to update.
 */
void Menu_SetEnterAction(Menu_Context_t *content)
{
    Menu_SetAction(content, MENU_ACTION_ENTER);
}

/**
 * @brief Queues MENU_ACTION_ESCAPE (typically from back button ISR).
 * @param[in,out] content Menu context to update.
 */
void Menu_SetEscapeAction(Menu_Context_t *content)
{
    Menu_SetAction(content, MENU_ACTION_ESCAPE);
}


