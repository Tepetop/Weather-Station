/*
 * menu_description.h
 *
 *  Created on: Nov 4, 2024
 *      Author: Remik
 */

#ifndef INC_PCD8544_MENU_CONFIG_H_
#define INC_PCD8544_MENU_CONFIG_H_

#include <PCD8544_Menu.h>

/*		Definition of main menu structures		*/
Menu_t menu1;
Menu_t menu2;
Menu_t menu3;
Menu_t menu4;
Menu_t menu5;
Menu_t menu6;
Menu_t menu7;
Menu_t menu8;
Menu_t menu9;
Menu_t menu10;

/*		Definition of sub menu structures		*/
Menu_t subMenu1;
Menu_t subMenu2;
Menu_t subMenu3;
Menu_t subMenu4;
Menu_t subMenu5;
Menu_t subMenu6;
Menu_t subMenu7;
Menu_t subMenu8;
Menu_t subMenu9;
Menu_t subMenu10;


/*				Declaration of menu structures					*/
//				name;   details;  next;   prev;   child;  parent; menuFunction;

Menu_t menu1 = {"Menu 1", "Opis 1", &menu2, NULL, &subMenu1, NULL, NULL};
	Menu_t menuReturn1 = {"POWROT", NULL, NULL, NULL, NULL, &menu1, NULL};
	Menu_t subMenu1 = {"Sub menu 1", "Opis 1", NULL, NULL, NULL, &menu1, NULL};

Menu_t menu2 = {"Menu 2", "Opis 2", &menu3, &menu1, &subMenu2, NULL, NULL};
	Menu_t menuReturn2 = {"POWROT", NULL, NULL, NULL, NULL, &menu2, NULL};
	Menu_t subMenu2 = {"Sub Menu 2", "Opis 2", NULL, NULL, NULL, &menu2, NULL};

Menu_t menu3 = {"Menu 3", "Opis 3", &menu4, &menu2, &subMenu3, NULL, NULL};
	Menu_t menuReturn3 = {"POWROT", NULL, NULL, NULL, NULL, &menu3, NULL};
	Menu_t subMenu3 = {"Sub Menu 3", "Opis 3", NULL, NULL, NULL, &menu3, NULL};

Menu_t menu4 = {"Menu 4", "Opis 4", &menu5, &menu3, &subMenu4, NULL, NULL};
	Menu_t menuReturn4 = {"POWROT", NULL, NULL, NULL, NULL, &menu4, NULL};
	Menu_t subMenu4 = {"Sub Menu 4", "Opis 4", NULL, NULL, NULL, &menu4, NULL};

Menu_t menu5 = {"Menu 5", "Opis 5", &menu6, &menu4, &subMenu5, NULL, NULL};
	Menu_t menuReturn5 = {"POWROT", NULL, NULL, NULL, NULL, &menu5, NULL};
	Menu_t subMenu5 = {"Sub Menu 5", "Opis 5", NULL, NULL, NULL, &menu5, NULL};

Menu_t menu6 = {"Menu 6", "Opis 6", &menu7, &menu5, &subMenu6, NULL, NULL};
	Menu_t menuReturn6 = {"POWROT", NULL, NULL, NULL, NULL, &menu6, NULL};
	Menu_t subMenu6 = {"Sub Menu 6", "Opis 6", NULL, NULL, NULL, &menu6, NULL};

Menu_t menu7 = {"Menu 7", "Opis 7", &menu8, &menu6, &subMenu7, NULL, NULL};
	Menu_t menuReturn7 = {"POWROT", NULL, NULL, NULL, NULL, &menu7, NULL};
	Menu_t subMenu7 = {"Sub Menu 7", "Opis 7", NULL, NULL, NULL, &menu7, NULL};

Menu_t menu8 = {"Menu 8", "Opis 8", &menu9, &menu7, &subMenu8, NULL, NULL};
	Menu_t menuReturn8 = {"POWROT", NULL, NULL, NULL, NULL, &menu8, NULL};
	Menu_t subMenu8 = {"Sub Menu 8", "Opis 8", NULL, NULL, NULL, &menu8, NULL};

Menu_t menu9 = {"Menu 9", "Opis 9", &menu10, &menu8, &subMenu9, NULL, NULL};
	Menu_t menuReturn9 = {"POWROT", NULL, NULL, NULL, NULL, &menu9, NULL};
	Menu_t subMenu9 = {"Sub Menu 9", "Opis 9", NULL, NULL, NULL, &menu9, NULL};

Menu_t menu10 = {"Menu 10", "Opis 10", NULL, &menu9, &subMenu10, NULL, NULL};
	Menu_t menuReturn10 = {"POWROT", NULL, NULL, NULL, NULL, &menu10, NULL};
	Menu_t subMenu10 = {"Sub Menu 10", "Opis 10", NULL, NULL, NULL, &menu10, NULL};


#endif /* INC_PCD8544_MENU_CONFIG_H_ */
