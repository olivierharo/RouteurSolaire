/*
  MaxPV_ESP.ino - ESP8266 program that provides a web interface and a API for EcoPV 3+
  Copyright (C) 2022 - Bernard Legrand.

  https://github.com/Jetblack31/

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU Lesser General Public License as published
  by the Free Software Foundation, either version 2.1 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU Lesser General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.

*/

/*************************************************************************************
**                                                                                  **
**        Ce programme fonctionne sur ESP8266 de tye Wemos avec 4 Mo de mémoire     **
**        La compilation s'effectue avec l'IDE Arduino                              **
**        Site Arduino : https://www.arduino.cc                                     **
**                                                                                  **
**************************************************************************************/

// ***********************************************************************************
// ******************            OPTIONS DE COMPILATION                ***************
// ***********************************************************************************


// ***********************************************************************************
// ******************        FIN DES OPTIONS DE COMPILATION            ***************
// ***********************************************************************************


// ***********************************************************************************
// ******************                   LIBRAIRIES                     ***************
// ***********************************************************************************

#include <LittleFS.h>
#include <ArduinoJson.h>            
#include <TickerScheduler.h>
#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <ESPAsyncWebServer.h>
#include <ESPAsyncWiFiManager.h>    
#include <AsyncElegantOTA.h>
#include <DNSServer.h>
#include <NTPClient.h>
#include <SimpleFTPServer.h>

// ***      ATTENTION : NE PAS ACTIVER LE DEBUG SERIAL SUR AUCUNE LIBRAIRIE        ***

// ***********************************************************************************
// ******************               FIN DES LIBRAIRIES                 ***************
// ***********************************************************************************



// ***********************************************************************************
// ************************    DEFINITIONS ET DECLARATIONS     ***********************
// ***********************************************************************************

// ***********************************************************************************
// ****************************   Définitions générales   ****************************
// ***********************************************************************************

#define MAXPV_VERSION       "3.0"
#define MAXPV_VERSION_FULL  "MaxPv! 3.0"
#define GMT_OFFSET           0        // Heure solaire

// SSID pour le Config Portal
#define SSID_CP             "MaxPV"

// login et password pour le service FTP
#define LOGIN_FTP           "maxpv"
#define PWD_FTP             "maxpv"

#define TELNET_PORT         23
#define SERIAL_BAUD         500000    // Vitesse de la liaison port série pour la connexion avec l'arduino
#define SERIALTIMEOUT       100       // Timeout pour les interrogations sur liaison série en ms
#define SERIAL_BUFFER       256       // Taille du buffer RX pour la connexion avec l'arduino (256 max)

#define OFF                 0
#define ON                  1
#define STOP                0
#define FORCE               1
#define AUTOM               9

// définition de l'ordre des paramètres de configuration de EcoPV tels que transmis
// et de l'index de rangement dans le tableau de stockage (début à la position 1)
// on utilise la position 0 pour stocker la version
#define NB_PARAM       17             // Nombre de paramètres transmis par EcoPV (17 = 16 + VERSION)
#define ECOPV_VERSION   0
#define V_CALIB         1
#define P_CALIB         2
#define PHASE_CALIB     3
#define P_OFFSET        4
#define P_RESISTANCE    5
#define P_MARGIN        6
#define GAIN_P          7
#define GAIN_I          8
#define E_RESERVE       9
#define P_DIV2_ACTIVE  10
#define P_DIV2_IDLE    11
#define T_DIV2_ON      12
#define T_DIV2_OFF     13
#define T_DIV2_TC      14
#define CNT_CALIB      15
#define P_INSTALLPV    16

// définition de l'ordre des informations statistiques transmises par EcoPV
// et de l'index de rangement dans le tableau de stockage (début à la position 1)
// on utilise la position 0 pour stocker la version
#define NB_STATS      23     // Nombre d'informations statistiques transmis par EcoPV
//#define ECOPV_VERSION 0
#define V_RMS         1
#define I_RMS         2
#define P_ACT         3
#define P_APP         4
#define P_ROUTED      5
#define P_IMP         6
#define P_EXP         7
#define COS_PHI       8
#define INDEX_ROUTED       9
#define INDEX_IMPORT       10
#define INDEX_EXPORT       11
#define INDEX_IMPULSION    12
#define P_IMPULSION        13
#define TRIAC_MODE         14
#define RELAY_MODE         15
#define DELAY_MIN          16
#define DELAY_AVG          17
#define DELAY_MAX          18
#define BIAS_OFFSET        19
#define STATUS_BYTE        20
#define ONTIME             21
#define SAMPLES            22


// ***********************************************************************************
// ************************ Déclaration des variables globales ***********************
// ***********************************************************************************

// Stockage des informations de EcoPV

String ecoPVConfig [ NB_PARAM ];
String ecoPVStats  [ NB_STATS ];
String ecoPVConfigAll;
String ecoPVStatsAll;

TickerScheduler ts ( 8 );     // Définition du nombre de tâches de Ticker



// Variables de configuration de MaxPV!
// Configuration IP statique
char static_ip[16]   = "192.168.1.250";
char static_gw[16]   = "192.168.1.1";
char static_sn[16]   = "255.255.255.0";
char static_dns1[16] = "192.168.1.1";
char static_dns2[16] = "8.8.8.8";

// Port HTTP                  // Attention, le choix du port est inopérant dans cette version
uint16_t httpPort = 80;

// Fin des variables de la configuration MaxPV!


// Flag indiquant la nécessité de sauvegarder la configuration de MaxPV!
bool shouldSaveConfig = false;

// Flag indiquant la nécessité de lire les paramètres de routage EcoPV
bool shouldReadParams = false;

// Variables pour surveiller que l'on garde le contact avec EcoPV dans l'Arduino Nano
unsigned long refTimeContactEcoPV = millis ( );
bool contactEcoPV = false;

// buffer pour manipuler le fichier de configuration de MaxPV! (ajuster la taille en fonction des besoins)
StaticJsonDocument <1024> jsonConfig;

IPAddress _ip, _gw, _sn, _dns1, _dns2;



// ***********************************************************************************
// ************************ DECLARATION DES SERVEUR ET CLIENTS ***********************
// ***********************************************************************************

AsyncWebServer webServer ( 80 );
DNSServer dnsServer;
WiFiServer telnetServer ( TELNET_PORT );
WiFiClient tcpClient;
WiFiUDP ntpUDP;
NTPClient timeClient ( ntpUDP, "europe.pool.ntp.org", 3600 * GMT_OFFSET, 600000 );
FtpServer ftpSrv;

// ***********************************************************************************
// **********************  FIN DES DEFINITIONS ET DECLARATIONS  **********************
// ***********************************************************************************




// ***********************************************************************************
// ***************************   FONCTIONS ET PROCEDURES   ***************************
// ***********************************************************************************

/////////////////////////////////////////////////////////
// setup                                               //
// Routine d'initialisation générale                   //
/////////////////////////////////////////////////////////
void setup ( ) {
  unsigned long refTime = millis ( );
  boolean       APmode  = true;

  // Début du debug sur liaison série
  Serial.begin ( 115200 );
  Serial.println ( F("\nMaxPV! par Bernard Legrand (2022).") );
  Serial.print ( F("Version : ") );
  Serial.println ( MAXPV_VERSION );
  Serial.println ( );

  // On teste l'existence du système de fichier
  // et sinon on formatte le système de fichier
  if ( !LittleFS.begin ( ) ) {
    Serial.println ( F("Système de fichier absent, formatage...") );
    LittleFS.format ( );
    if ( LittleFS.begin ( ) )
      Serial.println ( F("Système de fichier prêt et monté !") );
    else {
      Serial.println ( F("Erreur de préparation du système de fichier, redémarrage...") );
      delay ( 1000 );
      ESP.restart ( );
    }
  }
  else Serial.println ( F("Système de fichier prêt et monté !") );

  Serial.println ( );

  // On teste l'existence du fichier de configuration de MaxPV!
  if ( LittleFS.exists ( F("/config.json") ) ) {
    Serial.println ( F("Fichier de configuration présent, lecture de la configuration...") );
    if ( configRead ( ) ) {
      Serial.println ( F("Configuration lue et appliquée !") );
      APmode = false;
    }
    else {
      Serial.println ( F("Fichier de configuration incorrect, effacement du fichier et redémarrage...") );
      LittleFS.remove ( F("/config.json") );
      delay ( 1000 );
      ESP.restart ( );
    }
  }
  else Serial.println ( F("Fichier de configuration absent, démarrage en mode point d'accès pour la configuration réseau...") );

  Serial.println ( );

  _ip.fromString ( static_ip );
  _gw.fromString ( static_gw );
  _sn.fromString ( static_sn );
  _dns1.fromString ( static_dns1 );
  _dns2.fromString ( static_dns2 );

  AsyncWiFiManager wifiManager ( &webServer, &dnsServer );

  wifiManager.setAPCallback ( configModeCallback );
  wifiManager.setSaveConfigCallback ( saveConfigCallback );
  wifiManager.setDebugOutput ( true );
  wifiManager.setSTAStaticIPConfig ( _ip, _gw, _sn, _dns1, _dns2 );
  //wifiManager.setAPStaticIPConfig ( IPAddress ( 192, 168, 4, 1 ), IPAddress ( 192, 168, 4, 1 ), IPAddress ( 255, 255, 255, 0 ) );

  if ( APmode ) {    // Si on démarre en mode point d'accès / on efface le dernier réseau wifi connu pour forcer le mode AP
    wifiManager.resetSettings();
  }

  else {            // on devrait se connecter au réseau local avec les paramètres connus
    Serial.print   ( F("Tentative de connexion au dernier réseau connu...") );
    Serial.println ( F("Configuration IP, GW, SN, DNS1, DNS2 :") );
    Serial.println ( _ip.toString() );
    Serial.println ( _gw.toString() );
    Serial.println ( _sn.toString() );
    Serial.println ( _dns1.toString() );
    Serial.println ( _dns2.toString() );
  }

  wifiManager.autoConnect ( SSID_CP );

  Serial.println (  );
  Serial.println ( F("Connecté au réseau local en utilisant les paramètres IP, GW, SN, DNS1, DNS2 :") );
  Serial.println ( WiFi.localIP() );
  Serial.println ( WiFi.gatewayIP() );
  Serial.println ( WiFi.subnetMask() );
  Serial.println ( WiFi.dnsIP(0) );
  Serial.println ( WiFi.dnsIP(1) );

  // Mise à jour des variables globales de configuration IP (systématique même si pas de changement)
  WiFi.localIP ( ).toString ( ).toCharArray ( static_ip, 16 );
  WiFi.gatewayIP ( ).toString ( ).toCharArray ( static_gw, 16 );
  WiFi.subnetMask ( ).toString ( ).toCharArray ( static_sn, 16 );
  WiFi.dnsIP ( 0 ).toString ( ).toCharArray ( static_dns1, 16 );
  WiFi.dnsIP ( 1 ).toString ( ).toCharArray ( static_dns2, 16 );

  // Sauvegarde de la configuration si nécessaire
  if ( shouldSaveConfig ) {
    configWrite ( );
    Serial.println ( F("\nConfiguration sauvegardée !") );
  }

  Serial.println ( F("\n\n***** Le debug se poursuit en connexion telnet *****") );
  Serial.print ( F("Dans un terminal : nc ") );
  Serial.print ( static_ip );
  Serial.print ( F(" ") );
  Serial.println ( TELNET_PORT );

  // Démarrage du service TELNET
  telnetServer.begin ( );
  telnetServer.setNoDelay ( true );

  // Attente de 5 secondes pour permettre la connexion TELNET
  refTime = millis ( );
  while ( ( !telnetDiscoverClient ( ) ) && ( ( millis ( ) - refTime ) < 5000 ) ) {
    delay ( 200 );
    Serial.print ( F(".") );
  }

  // Fermeture du debug serial et fermeture de la liaison série
  wifiManager.setDebugOutput ( false );
  Serial.println ( F("\nFermeture de la connexion série de debug et poursuite du démarrage...") );
  Serial.println ( F("Bye bye !\n") );
  delay ( 100 );
  Serial.end ( );

  tcpClient.println ( F("\n***** Reprise de la transmission du debug *****\n") );
  tcpClient.println ( F("Connexion au réseau wifi réussie !") );
  tcpClient.println( F("\nConfiguration des services web...") );



      // ***********************************************************************
      // ********      DECLARATIONS DES HANDLERS DU SERVEUR WEB         ********
      // ***********************************************************************

  webServer.onNotFound ( []( AsyncWebServerRequest * request ) {
      request->redirect("/");
  } );


  webServer.on( "/", HTTP_ANY, []( AsyncWebServerRequest * request ) {
    String response = "";
    if ( LittleFS.exists ( F("/main.html") ) ) {
      request->send ( LittleFS, F("/main.html") );
    }
    else {
      response = F("Site Web non trouvé. Filesystem non chargé. Allez à : http://");
      response += WiFi.localIP().toString();
      response += F("/update pour uploader le filesystem.");
      request->send ( 200, "text/plain", response );
    }
  });


  webServer.on( "/index.html", HTTP_ANY, []( AsyncWebServerRequest * request ) {
    String response = "";
    if ( LittleFS.exists ( F("/index.html") ) ) {
      request->send ( LittleFS, F("/index.html") );
    }
    else {
      response = F("Site Web non trouvé. Filesystem non chargé. Allez à : http://");
      response += WiFi.localIP().toString();
      response += F("/update pour uploader le filesystem.");
      request->send ( 200, "text/plain", response );
    }
  });


  webServer.on( "/configuration.html", HTTP_ANY, []( AsyncWebServerRequest * request ) {
    request->send ( LittleFS, F("/configuration.html") );
  });


  webServer.on( "/main.html", HTTP_ANY, []( AsyncWebServerRequest * request ) {
    request->send ( LittleFS, F("/main.html") );
  });


  webServer.on( "/admin.html", HTTP_ANY, []( AsyncWebServerRequest * request ) {
    request->send ( LittleFS, F("/admin.html") );
  });


  webServer.on( "/credits.html", HTTP_ANY, []( AsyncWebServerRequest * request ) {
    request->send ( LittleFS, F("/credits.html") );
  });


  webServer.on( "/wizard.html", HTTP_ANY, []( AsyncWebServerRequest * request ) {
    request->send ( LittleFS, F("/wizard.html") );
  });


  webServer.on( "/maj.html", HTTP_ANY, []( AsyncWebServerRequest * request ) {
    //request->send ( LittleFS, F("/maj") );
    request->redirect("/update");
  });


  webServer.on( "/graph.html", HTTP_ANY, []( AsyncWebServerRequest * request ) {
    request->send ( LittleFS, F("/graph.html") );
  });


  webServer.on( "/maxpv.css", HTTP_ANY, []( AsyncWebServerRequest * request ) {
    request->send ( LittleFS, F("/maxpv.css"), "text/css" );
  });


  webServer.on( "/favicon.ico", HTTP_ANY, []( AsyncWebServerRequest * request ) {
    request->send ( LittleFS, F("/favicon.ico"), "image/png" );
  });


      // ***********************************************************************
      // ********                  HANDLERS DE L'API                    ********
      // ***********************************************************************

  webServer.on( "/api/action", HTTP_GET, []( AsyncWebServerRequest * request ) {
    String response = F("Request successfully processed");
    if ( request->hasParam ( "restart" ) )
      restartEcoPV ( );
    else if ( request->hasParam ( "resetindex" ) )
      resetIndexEcoPV ( );
    else if ( request->hasParam ( "saveindex" ) )
      saveIndexEcoPV ( );
    else if ( request->hasParam ( "saveparam" ) )
      saveConfigEcoPV ( );
    else if ( request->hasParam ( "loadparam" ) )
      loadConfigEcoPV ( );
    else if ( request->hasParam ( "format" ) )
      formatEepromEcoPV ( );
    else if ( request->hasParam ( "eraseconfigesp" ) )
      LittleFS.remove ( "/config.json" );
    else if ( request->hasParam ( "rebootesp" ) )
      rebootESP ( );
    else response = F("Unknown request");
    request->send ( 200, "text/plain", response );
  } );


  webServer.on ( "/api/get", HTTP_GET, []( AsyncWebServerRequest * request ) {
    String response = "";
    if ( request->hasParam ( "configmaxpv" ) )
      request->send ( LittleFS, F("/config.json"), "text/plain" );
    else if ( request->hasParam ( "versionweb" ) )
      request->send ( LittleFS, F("/versionweb.txt"), "text/plain" );
    else {
      if ( ( request->hasParam ( "param" ) ) && ( request->getParam("param")->value().toInt() > 0 ) && ( request->getParam("param")->value().toInt() <= ( NB_PARAM - 1 ) ) )
        response = ecoPVConfig [ request->getParam("param")->value().toInt() ];
      else if ( ( request->hasParam ( "data" ) ) && ( request->getParam("data")->value().toInt() > 0 ) && ( request->getParam("data")->value().toInt() <= ( NB_STATS - 1 ) ) )
        response = ecoPVStats [ request->getParam("data")->value().toInt() ];
      else if ( request->hasParam ( "allparam" ) )
        response = ecoPVConfigAll;
      else if ( request->hasParam ( "alldata" ) )
        response = ecoPVStatsAll;
      else if ( request->hasParam ( "version" ) )
        response = ecoPVConfig [ 0 ];
      else if ( request->hasParam ( "versionmaxpv" ) )
        response = MAXPV_VERSION;
      else if ( request->hasParam ( "relaystate" ) ) {
        if ( ecoPVStats[RELAY_MODE].toInt() == STOP ) response = F("STOP");
        else if ( ecoPVStats[RELAY_MODE].toInt() == FORCE ) response = F("FORCE");
        else if ( ecoPVStats[STATUS_BYTE].toInt() & B00000100 ) response = F("ON");
        else response = F("OFF");
      }
      else if ( request->hasParam ( "ssrstate" ) ) {
        if ( ecoPVStats[TRIAC_MODE].toInt() == STOP ) response = F("STOP");
        else if ( ecoPVStats[TRIAC_MODE].toInt() == FORCE ) response = F("FORCE");
        else if ( ecoPVStats[TRIAC_MODE].toInt() == AUTOM ) {
          if ( ecoPVStats[STATUS_BYTE].toInt() & B00000010 ) response = F("MAX");
          else if ( ecoPVStats[STATUS_BYTE].toInt() & B00000001 ) response = F("ON");
          else response = F("OFF");
        }
      }
      else if ( request->hasParam ( "ping" ) )
        if ( contactEcoPV ) response = F("running");
        else response = F("offline");
      else if ( request->hasParam ( "time" ) )
        response = timeClient.getFormattedTime ( );
      else response = F("Unknown request");
      request->send ( 200, "text/plain", response );
    }
  } );


  webServer.on ( "/api/set", HTTP_GET, []( AsyncWebServerRequest * request ) {
    String response = F("Request successfully processed");
    String mystring = "";
    if ( ( request->hasParam ( "param" ) ) && ( request->hasParam ( "value" ) )
         && ( request->getParam("param")->value().toInt() > 0 ) && ( request->getParam("param")->value().toInt() <= ( NB_PARAM - 1 ) ) ) {
      mystring = request->getParam("value")->value();
      mystring.replace( ",", "." );
      mystring.trim();
      setParamEcoPV ( request->getParam("param")->value(), mystring );
      // Note : la mise à jour de la base interne de MaxPV se fera de manière asynchrone
      // setParamEcoPV() demande à EcoPV de renvoyer tous les paramètres à MaxPV
    }
    else if ( ( request->hasParam ( "relaymode" ) ) && ( request->hasParam ( "value" ) ) ) {
      mystring = request->getParam("value")->value();
      mystring.trim();
      if ( mystring == F("stop") ) relayModeEcoPV ( STOP );
      else if ( mystring == F("force") ) relayModeEcoPV ( FORCE );
      else if ( mystring == F("auto") ) relayModeEcoPV ( AUTOM );
      else response = F("Bad request");
    }
    else if ( ( request->hasParam ( "ssrmode" ) ) && ( request->hasParam ( "value" ) ) ) {
      mystring = request->getParam("value")->value();
      mystring.trim();
      if ( mystring == F("stop") ) SSRModeEcoPV ( STOP );
      else if ( mystring == F("force") ) SSRModeEcoPV ( FORCE );
      else if ( mystring == F("auto") ) SSRModeEcoPV ( AUTOM );
      else response = F("Bad request");
    }
    else if ( ( request->hasParam ( "configmaxpv" ) ) && ( request->hasParam ( "value" ) ) ) {
      mystring = request->getParam("value")->value();
      deserializeJson ( jsonConfig, mystring );
      strlcpy ( static_ip,
                jsonConfig ["ip"],
                16);
      strlcpy ( static_gw,
                jsonConfig ["gateway"],
                16);
      strlcpy ( static_sn,
                jsonConfig ["subnet"],
                16);
      strlcpy ( static_dns1,
                jsonConfig ["dns1"],
                16);
      strlcpy ( static_dns2,
                jsonConfig ["dns2"],
                16);
      httpPort = jsonConfig["http_port"];
      configWrite ( );
    }
    else response = F("Bad request or request unknown");
    request->send ( 200, "text/plain", response );
  } );


      // ***********************************************************************
      // ********                   FIN DES HANDLERS                    ********
      // ***********************************************************************



  // Démarrage du service update OTA et des services réseaux
  AsyncElegantOTA.setID ( MAXPV_VERSION_FULL );
  AsyncElegantOTA.begin ( &webServer );
  webServer.begin ( );
  timeClient.begin ( );

  tcpClient.println ( F("Services web configurés et démarrés !") );
  tcpClient.print ( F("Port web : ") );
  tcpClient.println ( httpPort );

  // Démarrage du service FTP
  ftpSrv.begin ( LOGIN_FTP, PWD_FTP );

  tcpClient.println ( F("Service FTP configuré et démarré !") );
  tcpClient.println ( F("Port FTP : 21") );
  tcpClient.print ( F("Login FTP : ") );
  tcpClient.print ( LOGIN_FTP );
  tcpClient.print ( F("  password FTP : ") );
  tcpClient.println ( PWD_FTP );

  tcpClient.println( F("\nDémarrage de la connexion à l'Arduino...") );

  Serial.setRxBufferSize ( SERIAL_BUFFER );
  Serial.begin ( SERIAL_BAUD );
  Serial.setTimeout ( SERIALTIMEOUT );
  clearSerialInputCache;

  tcpClient.println ( F("Liaison série configurée pour la communication avec l'Arduino, en attente d'un contact...") );

  while ( !serialProcess ( ) ) {
    tcpClient.print ( F(".") );
  }
  tcpClient.println ( F("\nContact établi !\n") );
  contactEcoPV = true;

  // Premier peuplement des informations de configuration de EcoPV
  clearSerialInputCache ( );
  getAllParamEcoPV ( );
  delay ( 100 );
  serialProcess ( );
  clearSerialInputCache ( );
  getVersionEcoPV ( );
  delay ( 100 );
  serialProcess ( );

  tcpClient.println ( );
  tcpClient.println ( MAXPV_VERSION_FULL );
  tcpClient.print ( F("EcoPV version ") );
  tcpClient.println ( ecoPVConfig[ECOPV_VERSION] );
  if ( String ( MAXPV_VERSION ) != ecoPVConfig[ECOPV_VERSION] ) {
    tcpClient.println ( F("\n*****                !! ATTENTION !!                  *****") );
    tcpClient.println ( F("\n***** Version ECOPV et version MaxPV! différentes !!! *****") );
  };
  tcpClient.println ( F("\n*** Fin du Setup ***\n") );



      // ***********************************************************************
      // ********      DEFINITION DES TACHES RECURRENTES DE TICKER      ********
      // ***********************************************************************

  // Découverte d'une connexion TELNET
  ts.add ( 0,   533, [&](void *) {
    telnetDiscoverClient ( );
  }, nullptr, true );
  // Lecture et traitement des messages de l'Arduino sur port série
  ts.add ( 1,    70, [&](void *) {
    if ( Serial.available ( ) ) serialProcess ( );
  }, nullptr, true );
  // Forcage de l'envoi des paramètres du routeur EcoPV
  ts.add ( 2, 89234, [&](void *) {
    shouldReadParams = true;
  }, nullptr, true );
  // Mise à jour de l'horloge par NTP
  ts.add ( 3, 20003, [&](void *) {
    timeClient.update ( );
  }, nullptr, true );
  // Surveillance du fonctionnement du routeur et de l'Arduino
  ts.add ( 4,   617, [&](void *) {
    watchDogContactEcoPV ( );
  }, nullptr, true );
  // Traitement des tâches FTP
  ts.add ( 5,   307, [&](void *) {
    ftpSrv.handleFTP ( );
  }, nullptr, true );
  // Traitement des demandes de lecture des paramètres du routeur EcoPV
  ts.add ( 6,  2679, [&](void *) {
    if ( shouldReadParams ) getAllParamEcoPV ( );
  }, nullptr, true );
  // Affichage périodique d'informations de debug sur TELNET
  ts.add ( 7, 31234, [&](void *) {
    tcpClient.print ( F("Heap disponible : ") );
    tcpClient.print ( ESP.getFreeHeap ( ) );
    tcpClient.println ( F(" bytes") );
    tcpClient.print ( F("Heap fragmentation : ") );
    tcpClient.print ( ESP.getHeapFragmentation ( ) );
    tcpClient.println ( F(" %") );
  }, nullptr, true );

  delay ( 1000 );
}





///////////////////////////////////////////////////////////////////
// loop                                                          //
// Loop routine exécutée en boucle                               //
///////////////////////////////////////////////////////////////////
void loop ( ) {

  // Exécution des tâches récurrentes Ticker
  ts.update ( );

  dnsServer.processNextRequest();
  
}


///////////////////////////////////////////////////////////////////
// Fonctions                                                     //
// et Procédures                                                 //
///////////////////////////////////////////////////////////////////


bool configRead ( void ) {
  // Note ici on utilise un debug sur liaison série car la fonction n'est appelé qu'au début du SETUP
  Serial.println ( F("Lecture du fichier de configuration...") );
  File configFile = LittleFS.open ( F("/config.json"), "r" );
  if ( configFile ) {
    Serial.println ( F("Configuration lue !") );
    Serial.println ( F("Analyse...") );
    DeserializationError error = deserializeJson ( jsonConfig, configFile );
    if ( !error ) {
      Serial.println ( F("\nparsed json:") );
      serializeJsonPretty ( jsonConfig, Serial );
      if ( jsonConfig ["ip"] ) {
        Serial.println ( F("\n\nRestauration de la configuration IP...") );
        strlcpy ( static_ip,
                  jsonConfig ["ip"] | "192.168.1.250",
                  16);
        strlcpy ( static_gw,
                  jsonConfig ["gateway"] | "192.168.1.1",
                  16);
        strlcpy ( static_sn,
                  jsonConfig ["subnet"] | "255.255.255.0",
                  16);
        strlcpy ( static_dns1,
                  jsonConfig ["dns1"] | "192.168.1.1",
                  16);
        strlcpy ( static_dns2,
                  jsonConfig ["dns2"] | "8.8.8.8",
                  16);
        httpPort  = jsonConfig ["http_port"] | 80;
      }
      else {
        Serial.println( F("\n\nPas d'adresse IP dans le fichier de configuration !") );
        return false;
      }
    }
    else {
      Serial.println( F("Erreur durant l'analyse du fichier !") );
      return false;
    }
  }
  else {
    Serial.println ( F("Erreur de lecture du fichier !") );
    return false;
  }
  return true;
}



void configWrite ( void ) {
  jsonConfig["ip"]      = static_ip;
  jsonConfig["gateway"] = static_gw;
  jsonConfig["subnet"]  = static_sn;
  jsonConfig["dns1"]    = static_dns1;
  jsonConfig["dns2"]    = static_dns2;
  jsonConfig["http_port"] = httpPort;

  File configFile = LittleFS.open ( F("/config.json"), "w" );
  serializeJson ( jsonConfig, configFile );
  configFile.close ( );
}



void rebootESP ( void ) {
  delay ( 100 );
  ESP.reset ( );
  delay ( 1000 );
}



bool telnetDiscoverClient ( void ) {
  if ( telnetServer.hasClient ( ) ) {
    tcpClient = telnetServer.available ( );
    clearScreen ( );
    tcpClient.println ( F("\nMaxPV! par Bernard Legrand (2022).") );
    tcpClient.print ( F("Version : ") );
    tcpClient.println ( MAXPV_VERSION );
    tcpClient.println ( );
    tcpClient.println ( F("Configuration IP : ") );
    tcpClient.print ( F("Adresse IP : ") );
    tcpClient.println ( WiFi.localIP ( ) );
    tcpClient.print ( F("Passerelle : ") );
    tcpClient.println ( WiFi.gatewayIP ( ) );
    tcpClient.print ( F("Masque SR  : ") );
    tcpClient.println ( WiFi.subnetMask ( ) );
    tcpClient.print ( F("IP DNS 1   : ") );
    tcpClient.println ( WiFi.dnsIP ( 0 ) );
    tcpClient.print ( F("IP DNS 2   : ") );
    tcpClient.println ( WiFi.dnsIP ( 1 ) );
    tcpClient.print ( F("Port HTTP : ") );
    tcpClient.println ( httpPort );
    tcpClient.println ( F("Port FTP : 21") );
    return true;
  }
  return false;
}



void saveConfigCallback ( ) {
  Serial.println ( F("La configuration sera sauvegardée !") );
  shouldSaveConfig = true;
}



void configModeCallback ( AsyncWiFiManager *myWiFiManager )  {
  Serial.print ( F("Démarrage du mode point d'accès : ") );
  Serial.println ( myWiFiManager->getConfigPortalSSID ( ) );
  Serial.println ( F("Adresse du portail : ") );
  Serial.println ( WiFi.softAPIP ( ) );
}



void clearScreen ( void ) {
  tcpClient.write ( 27 );          // ESC
  tcpClient.print ( F("[2J") );    // clear screen
  tcpClient.write ( 27 );          // ESC
  tcpClient.print ( F("[H") );     // cursor to home
}



void clearSerialInputCache ( void ) {
  while ( Serial.available ( ) > 0 ) {
    Serial.read ( );
  }
}



bool serialProcess ( void ) {
#define END_OF_TRANSMIT   '#'
  int stringCounter = 0;
  int index = 0;
  
  // Les chaînes valides envoyées par l'arduino se terminent toujours par #
  String incomingData = Serial.readStringUntil ( END_OF_TRANSMIT );
  
  // On teste la validité de la chaîne qui doit contenir 'END' à la fin
  if ( incomingData.endsWith ( F("END") ) ) {
    tcpClient.print ( F("Réception de : ") );
    tcpClient.println ( incomingData );
    incomingData.replace ( F(",END"), "" );
    contactEcoPV = true;
    refTimeContactEcoPV = millis ( );

    if ( incomingData.startsWith ( F("STATS") ) ) {
      incomingData.replace ( F("STATS,"), "" );
      stringCounter++;        // on incrémente pour placer la première valeur à l'index 1
      while ( ( incomingData.length ( ) > 0 ) && ( stringCounter < NB_STATS ) ) {
        index = incomingData.indexOf ( ',' );
        if ( index == -1 ) {
          ecoPVStats[stringCounter++] = incomingData;
          break;
        }
        else {
          ecoPVStats[stringCounter++] = incomingData.substring ( 0, index );
          incomingData = incomingData.substring ( index + 1 );
        }
      }
      // Conversion des index en kWh
      ecoPVStats[INDEX_ROUTED] = String ( ( ecoPVStats[INDEX_ROUTED].toFloat ( ) / 1000.0 ), 3 );
      ecoPVStats[INDEX_IMPORT] = String ( ( ecoPVStats[INDEX_IMPORT].toFloat ( ) / 1000.0 ), 3 );
      ecoPVStats[INDEX_EXPORT] = String ( ( ecoPVStats[INDEX_EXPORT].toFloat ( ) / 1000.0 ), 3 );
      ecoPVStats[INDEX_IMPULSION] = String ( ( ecoPVStats[INDEX_IMPULSION].toFloat ( ) / 1000.0 ), 3 );

      ecoPVStatsAll = "";
      for (int i = 1; i < NB_STATS; i++) {
        ecoPVStatsAll += ecoPVStats[i];
        if ( i < ( NB_STATS - 1 ) ) ecoPVStatsAll += F(",");
      }
    }

    else if ( incomingData.startsWith ( F("PARAM") ) ) {
      incomingData.replace ( F("PARAM,"), "" );
      ecoPVConfigAll = incomingData;
      stringCounter++;        // on incrémente pour placer la première valeur Vrms à l'index 1
      while ( ( incomingData.length ( ) > 0 )  && ( stringCounter < NB_PARAM ) ) {
        index = incomingData.indexOf ( ',' );
        if ( index == -1 ) {
          ecoPVConfig[stringCounter++] = incomingData;
          break;
        }
        else {
          ecoPVConfig[stringCounter++] = incomingData.substring ( 0, index );
          incomingData = incomingData.substring ( index + 1 );
        }
      }
    }

    else if ( incomingData.startsWith ( F("VERSION") ) ) {
      incomingData.replace ( F("VERSION,"), "" );
      index = incomingData.indexOf ( ',' );
      if ( index != -1 ) {
        ecoPVConfig[ECOPV_VERSION] = incomingData.substring ( 0, index );
        ecoPVStats[ECOPV_VERSION] = ecoPVConfig[ECOPV_VERSION];
      }
    }
    return true;
  }
  else {
    return false;
  }
}



void formatEepromEcoPV ( void ) {
  Serial.print ( F("FORMAT,END#") );
}



void getAllParamEcoPV ( void ) {
  Serial.print ( F("PARAM,END#") );
  shouldReadParams = false;
}



void setParamEcoPV ( String param, String value ) {
  String command = "";
  if ( ( param.toInt() < 10 ) && ( !param.startsWith ("0") ) ) param = "0" + param;
  command = F("SETPARAM,") + param + F(",") + value + F(",END#") ;
  Serial.print ( command );
  shouldReadParams = true;    // on demande la lecture des paramètres contenus dans EcoPV
                              // pour mettre à jour dans MaxPV
                              // ce qui permet de vérifier la prise en compte de la commande
                              // C'est par ce seul moyen de MaxPV met à jour sa base interne des paramètres
}



void getVersionEcoPV ( void ) {
  Serial.print ( F("VERSION,END#") );
}



void saveConfigEcoPV ( void ) {
  Serial.print ( F("SAVECFG,END#") );
}



void loadConfigEcoPV ( void ) {
  Serial.print ( F("LOADCFG,END#") );
  shouldReadParams = true;    // on demande la lecture des paramètres contenus dans EcoPV
                              // pour mettre à jour dans MaxPV
                              // ce qui permet de vérifier la prise en compte de la commande
                              // C'est par ce seul moyen de MaxPV met à jour sa base interne des paramètres
}



void saveIndexEcoPV ( void ) {
  Serial.print ( F("SAVEINDX,END#") );
}



void resetIndexEcoPV ( void ) {
  Serial.print ( F("INDX0,END#") );
}



void restartEcoPV ( void ) {
  Serial.print ( F("RESET,END#") );
}



void relayModeEcoPV ( byte opMode ) {
  String command = F("SETRELAY,");
  if ( opMode == STOP ) command += F("STOP");
  if ( opMode == FORCE ) command += F("FORCE");
  if ( opMode == AUTOM ) command += F("AUTO");
  command += F(",END#");
  Serial.print ( command );
}



void SSRModeEcoPV ( byte opMode ) {
  String command = F("SETSSR,");
  if ( opMode == STOP ) command += F("STOP");
  if ( opMode == FORCE ) command += F("FORCE");
  if ( opMode == AUTOM ) command += F("AUTO");
  command += F(",END#");
  Serial.print ( command );
}



void watchDogContactEcoPV (void) {
  if ( ( millis ( ) - refTimeContactEcoPV ) > 1500  ) contactEcoPV = false;
}