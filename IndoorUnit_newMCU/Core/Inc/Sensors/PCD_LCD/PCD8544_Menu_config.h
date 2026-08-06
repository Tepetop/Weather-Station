/**
 * @file PCD8544_Menu_config.h
 * @brief Application menu tree definition for the indoor weather station.
 * @details Instantiates linked Menu_t nodes, wires parent/child/sibling links,
 *          and binds leaf items to WS_UI view callbacks.
 */

#ifndef INC_PCD8544_MENU_CONFIG_H_
#define INC_PCD8544_MENU_CONFIG_H_

#include <PCD8544_Menu.h>
#include <string.h>
#include "weather_station_ui.h"
#include "main.h"

/** @brief Shared escape handler that queues MENU_ACTION_ESCAPE for Menu_Task */
extern void Menu_EscapeWraper(void);

/* ============================================================================
 * Menu tree nodes (forward declarations)
 * ============================================================================ */

Menu_t StronaDomyslna;
Menu_t Ustawienia;
	/* Wykresy disabled — history lives on Pico dashboard (see WS_UI_CHARTS_ENABLED).
	Menu_t Wykresy;
		Menu_t tempWykres;
		Menu_t wilgWykres;
		Menu_t cisnWykres;
		Menu_t luxWykres;
		Menu_t powrotWykres;
	*/
	Menu_t StacjePomiarowe;
		Menu_t statusPomiarow;
		Menu_t wykonajPomiar;
		Menu_t powrotPomiar;
	Menu_t StacjaCentralna;
		Menu_t statusCentralna;
		Menu_t ustawieniaRTC;
		Menu_t powrotCentralna;
	Menu_t PowrotUstawienia;

/* ============================================================================
 * Menu tree initialization
 * ============================================================================ */

//					name;  					next;   		  	prev;   		   child;  			parent;		 		menuFunction;

Menu_t StronaDomyslna = {"Aktualne dane", 			&Ustawienia,      	NULL, 				NULL, 				NULL, 		 		WS_UI_MeasurementDisplay};
Menu_t Ustawienia = {"Ustawienia", 				NULL,  			  	&StronaDomyslna, 	&StacjePomiarowe, 	NULL, 		 		NULL};

	/* Wykresy disabled — keep tree for easy restore; charts gated by WS_UI_CHARTS_ENABLED.
	Menu_t Wykresy = {"Wykresy", 				&StacjePomiarowe, 	NULL, 				&tempWykres,		&Ustawienia, 		NULL};
		Menu_t tempWykres = {"Temperatura",		&wilgWykres, 	  	NULL, 				NULL, 				&Wykresy, 	 		WS_UI_ChartTemperature};
		Menu_t wilgWykres = {"Wilgotnosc", 		&cisnWykres, 	  	&tempWykres, 		NULL, 				&Wykresy, 	 		WS_UI_ChartHumidity};
		Menu_t cisnWykres = {"Cisnienie", 		&luxWykres, 	  	&wilgWykres, 		NULL, 				&Wykresy, 	 		WS_UI_ChartPressure};
		Menu_t luxWykres = {"Swiatlosc", 		&powrotWykres, 	    &cisnWykres, 		NULL, 				&Wykresy, 	 		WS_UI_ChartLux};
		Menu_t powrotWykres = {"Powrot", 		NULL, 	  		    &luxWykres, 		NULL, 				&Wykresy, 	 		Menu_EscapeWraper};
	*/

	Menu_t StacjePomiarowe = {"Stacje pom.",	&StacjaCentralna,   NULL, 				&statusPomiarow, 	&Ustawienia, 		NULL};
		Menu_t statusPomiarow = {"Status", 		&wykonajPomiar,     NULL, 				NULL, 				&StacjePomiarowe, 	WS_UI_StationsStatus};
		Menu_t wykonajPomiar = {"Wykonaj pom.",	&powrotPomiar,  	&statusPomiarow,  	NULL, 				&StacjePomiarowe, 	WS_UI_TakeMeasurement};
		Menu_t powrotPomiar = {"Powrot", 		NULL, 			  	&wykonajPomiar, 	NULL, 				&StacjePomiarowe, 	Menu_EscapeWraper};

	Menu_t StacjaCentralna = {"Stacja centr",	&PowrotUstawienia, &StacjePomiarowe,  	&statusCentralna, 	&Ustawienia, 		NULL};
		Menu_t statusCentralna = {"Status",   	&ustawieniaRTC,   NULL,  				NULL, 				&StacjaCentralna, 	WS_UI_CentralStatus};
		Menu_t ustawieniaRTC = {"Ustaw RTC",  	&powrotCentralna, &statusCentralna,  	NULL, 				&StacjaCentralna, 	WS_UI_SetRTC};
		Menu_t powrotCentralna = {"Powrot",   	NULL, 			  &ustawieniaRTC, 		NULL, 				&StacjaCentralna, 	Menu_EscapeWraper};

	Menu_t PowrotUstawienia = {"Powrot", 	  	NULL, 	  	  	&StacjaCentralna, 		NULL, 				&Ustawienia, 		Menu_EscapeWraper};

#endif /* INC_PCD8544_MENU_CONFIG_H_ */
