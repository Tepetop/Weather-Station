/**
 * @file PCD8544_Menu.h
 * @brief Linked-list menu navigation API for the PCD8544 display.
 * @details Declares menu tree structures, navigation state, encoder-driven
 *          actions, and display refresh helpers for the indoor unit UI.
 */

#ifndef INC_PCD8544_MENU_H_
#define INC_PCD8544_MENU_H_

#include <PCD8544.h>
#include <stdint.h>
#include "main.h"

/* ============================================================================
 * Configuration
 * ============================================================================ */

/** @brief Minimum cursor row index on the LCD */
#define MENU_MIN_CURSOR_ROW         0x00U
/** @brief Maximum submenu nesting depth supported by navigation stack */
#define MENU_MAX_DEPTH              5

/* ============================================================================
 * Types
 * ============================================================================ */

/** @brief Forward declaration for doubly-linked menu node */
typedef struct menu_s Menu_t;

/**
 * @brief Doubly-linked menu node with optional child subtree and action callback
 */
struct menu_s
{
	const char *name;              /**< Menu item label shown on the display */
	Menu_t *next;                  /**< Next sibling in the same menu level */
	Menu_t *prev;                  /**< Previous sibling in the same menu level */
	Menu_t *child;                 /**< First child item when entering a submenu */
	Menu_t *parent;                /**< Parent item when inside a submenu */
	void(*menuFunction)(void);     /**< Callback executed on leaf selection */
};

/**
 * @brief Menu navigation action for the state machine
 */
typedef enum
{
	MENU_ACTION_IDLE = 0x00U,  /**< No pending navigation action */
	MENU_ACTION_NEXT,          /**< Move selection to next item */
	MENU_ACTION_PREV,          /**< Move selection to previous item */
	MENU_ACTION_ENTER,         /**< Enter submenu or execute leaf callback */
	MENU_ACTION_ESCAPE         /**< Return to parent menu or exit view */
} Menu_Action_t;

/**
 * @brief Chart view type selected from the menu (when chart view is active)
 */
typedef enum {
    CHART_VIEW_NONE = 0,       /**< No chart view active */
    CHART_VIEW_TEMPERATURE,    /**< Temperature history chart */
    CHART_VIEW_HUMIDITY,       /**< Humidity history chart */
    CHART_VIEW_PRESSURE,       /**< Pressure history chart */
    CHART_VIEW_LUX             /**< Light intensity history chart */
} ChartViewType_t;

/**
 * @brief Runtime navigation state and special-view flags
 */
typedef struct
{
    uint8_t		MenuIndex;                              /**< Virtual index of current selection */
    uint8_t 	CursorPosOnLCD;                         /**< Cursor row within visible viewport */
    uint8_t		PrevMenuIndex[MENU_MAX_DEPTH];          /**< Saved MenuIndex per depth level */
    uint8_t		PrevLCDRowPos[MENU_MAX_DEPTH];          /**< Saved cursor row per depth level */
    uint8_t     CurrentDepth;                           /**< Current submenu nesting depth */
    uint8_t     InDetailsView;                          /**< Details overlay view active */
	uint8_t     InDefaultMeasurementsView;              /**< Default measurement screen active */
    uint8_t     InChartView;                            /**< Chart view active */
	uint8_t		InScreenSaver;                          /**< Screen saver view active */
	uint8_t     InStationsStatusView;                   /**< Outdoor stations status view active */
	uint8_t     InCentralStatusView;                    /**< Central unit status view active */
    ChartViewType_t ChartViewType;                      /**< Active chart type when InChartView is set */
	uint8_t 	actionPending;                            /**< Non-zero when an action awaits Menu_Task */
    Menu_Action_t currentAction;                        /**< Pending navigation action */
} Menu_Variables_t;

/**
 * @brief Menu context binding root menu tree to navigation state
 */
typedef struct
{
	Menu_t				*rootMenu;     /**< Current menu level head / selected branch */
	Menu_t				*defaultMenu;  /**< Root menu restored on full escape */
	Menu_Variables_t	state;         /**< Navigation and view state */
} Menu_Context_t;

/**
 * @brief Menu library operation status codes
 */
typedef enum
{
	Menu_OK = 0x00U,  /**< Operation completed successfully */
	Menu_Error        /**< Invalid pointer, depth limit, or navigation error */
} Menu_Status;

/* ============================================================================
 * Public API
 * ============================================================================ */

/**
 * @brief Initializes menu context with the root menu tree.
 * @param[in]  root    Root menu node (first top-level item).
 * @param[out] content Menu context to initialize.
 * @retval Menu_OK    Context initialized successfully.
 * @retval Menu_Error Invalid root or content pointer.
 */
Menu_Status Menu_Init(Menu_t *root, Menu_Context_t *content);

/**
 * @brief Applies encoder position delta to menu navigation state.
 * @param[in,out] PCD      Display driver used for cursor refresh.
 * @param[in]     Position Signed encoder step count (positive = next, negative = prev).
 * @param[in,out] content  Menu context to update.
 * @retval Menu_OK    Encoder input processed successfully.
 * @retval Menu_Error Invalid pointer or navigation failure.
 */
Menu_Status Menu_GetTickFromEncoder(PCD8544_t *PCD, int8_t Position, Menu_Context_t *content);

/**
 * @brief Updates inverted cursor highlight for the current selection.
 * @param[in,out] PCD     Display driver instance.
 * @param[in,out] content Menu context with current cursor position.
 * @retval Menu_OK    Cursor sign updated successfully.
 * @retval Menu_Error Invalid pointer.
 */
Menu_Status Menu_SetCursorSign(PCD8544_t *PCD, Menu_Context_t *content);

/**
 * @brief Renders the current menu level into the display framebuffer.
 * @param[in,out] PCD     Display driver instance.
 * @param[in,out] content Menu context with active menu branch.
 * @retval Menu_OK    Menu drawn to buffer successfully.
 * @retval Menu_Error Invalid pointer.
 */
Menu_Status Menu_RefreshDisplay(PCD8544_t *PCD, Menu_Context_t *content);

/**
 * @brief Moves selection to the next menu item and refreshes the display.
 * @param[in,out] PCD     Display driver instance.
 * @param[in,out] content Menu context to advance.
 * @retval Menu_OK    Selection moved or blocked at end of list.
 * @retval Menu_Error Invalid pointer or no next item available.
 */
Menu_Status Menu_Next(PCD8544_t *PCD, Menu_Context_t *content);

/**
 * @brief Moves selection to the previous menu item and refreshes the display.
 * @param[in,out] PCD     Display driver instance.
 * @param[in,out] content Menu context to move backward.
 * @retval Menu_OK    Selection moved or blocked at start of list.
 * @retval Menu_Error Invalid pointer or no previous item available.
 */
Menu_Status Menu_Previev(PCD8544_t *PCD, Menu_Context_t *content);

/**
 * @brief Enters a submenu, executes a leaf callback, or opens a details view.
 * @param[in,out] PCD     Display driver instance.
 * @param[in,out] content Menu context for the current selection.
 * @retval Menu_OK    Enter action handled successfully.
 * @retval Menu_Error Invalid pointer, depth limit, or missing child.
 */
Menu_Status Menu_Enter(PCD8544_t *PCD, Menu_Context_t *content);

/**
 * @brief Returns to the parent menu or exits a special overlay view.
 * @param[in,out] PCD     Display driver instance.
 * @param[in,out] content Menu context to restore from navigation stack.
 * @retval Menu_OK    Escape handled successfully.
 * @retval Menu_Error Invalid pointer or stack underflow.
 */
Menu_Status Menu_Escape(PCD8544_t *PCD, Menu_Context_t *content);

/**
 * @brief Processes one pending menu action from the navigation state machine.
 * @param[in,out] PCD     Display driver instance.
 * @param[in,out] content Menu context with actionPending flag.
 * @retval Menu_OK    Task step completed (action cleared when handled).
 * @retval Menu_Error Invalid pointer.
 */
Menu_Status Menu_Task(PCD8544_t *PCD, Menu_Context_t *content);

/**
 * @brief Queues a generic menu navigation action (safe from interrupt context).
 * @param[in,out] content Menu context to update.
 * @param[in]     action  Navigation action to queue.
 */
void Menu_SetAction(Menu_Context_t *content, Menu_Action_t action);

/**
 * @brief Queues MENU_ACTION_NEXT (typically from encoder or button ISR).
 * @param[in,out] content Menu context to update.
 */
void Menu_SetNextAction(Menu_Context_t *content);

/**
 * @brief Queues MENU_ACTION_PREV (typically from encoder or button ISR).
 * @param[in,out] content Menu context to update.
 */
void Menu_SetPrevAction(Menu_Context_t *content);

/**
 * @brief Queues MENU_ACTION_ENTER (typically from select button ISR).
 * @param[in,out] content Menu context to update.
 */
void Menu_SetEnterAction(Menu_Context_t *content);

/**
 * @brief Queues MENU_ACTION_ESCAPE (typically from back button ISR).
 * @param[in,out] content Menu context to update.
 */
void Menu_SetEscapeAction(Menu_Context_t *content);

#endif /* INC_PCD8544_MENU_H_ */
