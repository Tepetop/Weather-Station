/*
 * menu_description.h
 *
 *  Created on: Nov 4, 2024
 *      Author: Tepetop
 */

#ifndef INC_PCD8544_MENU_CONFIG_H_
#define INC_PCD8544_MENU_CONFIG_H_

#include <PCD8544_Menu.h>
#include <string.h>
#include "weather_station.h"
#include "main.h"

extern void Menu_EscapeWraper(void);

/*		Definicja struktur 		*/
Menu_t StronaDomyslna;
Menu_t Ustawienia;
	Menu_t powrotUstawienia;
	Menu_t Wykresy;
		Menu_t powrotWykres;
		Menu_t tempWykres;
		Menu_t wilgWykres;
		Menu_t cisnWykres;
		Menu_t luxWykres;
	Menu_t StacjePomiarowe;
		Menu_t powrotPomiar;
		Menu_t statusPomiarow;
	Menu_t WykonajPomiar;

/*   																	MENU POMIAROWE														*/

//					name;  					next;   		  prev;   		   		child;  		parent;		 		menuFunction;

Menu_t StronaDomyslna = {"Dane pom.", 		&Ustawienia,      NULL, 				NULL, 			NULL, 		 		WS_UI_MeasurementDisplay};
Menu_t Ustawienia = {"Ustawienia", 			NULL,  			  &StronaDomyslna, 		&powrotUstawienia, 		NULL, 		 		NULL};
	Menu_t powrotUstawienia = {"Powrot", 	&Wykresy, 	  	  NULL, 				NULL, 			&Ustawienia, 		Menu_EscapeWraper}; 		// Opcja powrotu do menu Ustawienia
	Menu_t Wykresy = {"Wykresy", 			&StacjePomiarowe, &powrotUstawienia, 	&powrotWykres,	&Ustawienia, 		NULL};
		Menu_t powrotWykres = {"Powrot", 	&tempWykres, 	  NULL, 				NULL, 			&Wykresy, 	 		Menu_EscapeWraper}; 		// Opcja powrotu do menu Wykresy
		Menu_t tempWykres = {"Temperatura", &wilgWykres, 	  &powrotWykres, 		NULL, 			&Wykresy, 	 		WS_UI_ChartTemperature};
		Menu_t wilgWykres = {"Wilgotnosc", 	&cisnWykres, 	  &tempWykres, 			NULL, 			&Wykresy, 	 		WS_UI_ChartHumidity};
		Menu_t cisnWykres = {"Cisnienie", 	&luxWykres, 	  &wilgWykres, 			NULL, 			&Wykresy, 	 		WS_UI_ChartPressure};
		Menu_t luxWykres = {"Swiatlosc", 	NULL, 	  	 	  &cisnWykres, 			NULL, 			&Wykresy, 	 		WS_UI_ChartLux};
	Menu_t StacjePomiarowe = {"Stacje pom.",&WykonajPomiar,   &Wykresy, 			&powrotPomiar, 	&Ustawienia, 		NULL};
		Menu_t powrotPomiar = {"Powrot", 	&statusPomiarow,  NULL, 				NULL, 			&StacjePomiarowe, 	Menu_EscapeWraper}; 		// Opcja powrotu do menu Wykresy
		Menu_t statusPomiarow = {"Status", 	NULL,   	 	  &powrotPomiar, 		NULL, 			&StacjePomiarowe, 	WS_UI_StationsStatus};
	Menu_t WykonajPomiar = {"Wykonaj pom.",	NULL, 			 &StacjePomiarowe,  	NULL, 			&Ustawienia, 		WS_UI_TakeMeasurement};


#endif /* INC_PCD8544_MENU_CONFIG_H_ */
