#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <XPT2046_Touchscreen.h>  

//touch f si la pantalla no touchea,t si la pantalla si touchea
const bool USAR_TOUCH = true;
#define TOUCH_CS 21                 
XPT2046_Touchscreen ts(TOUCH_CS);  
#define TS_XMIN 200
#define TS_XMAX 3800
#define TS_YMIN 200
#define TS_YMAX 3800

#define SERVICE_UUID           "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHARACTERISTIC_UUID_RX "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHARACTERISTIC_UUID_TX "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"

BLECharacteristic *pCharacteristic;
bool dispositivoConectado = false;
#define QN 8
String colaCmd[QN];
volatile uint8_t qH = 0, qT = 0;
void encolar(const String &c) { uint8_t n = (qH + 1) % QN; if (n == qT) return; colaCmd[qH] = c; qH = n; }
bool telPendiente = true, slotsPendiente = true;
unsigned long ultimaTel = 0;

#define TFT_CS   5
#define TFT_DC   22
#define TFT_RST  4
#define SPI_MOSI 23
#define SPI_MISO 19
#define SPI_SCK  18
Adafruit_ILI9341 tft = Adafruit_ILI9341(TFT_CS, TFT_DC, TFT_RST);

#define ONE_WIRE_BUS 13
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);

const int RELAY_SUMINISTRO = 25;
const int RELAY_RECIRC     = 26;
const int RELAY_VENTILADOR = 32;
const int RELAY_COMPRESOR  = 33;

bool estadoSuministro = false;
bool estadoRecirc     = false;
bool estadoVentilador = false;
bool estadoCompresor  = false;

int pantallaActual = 0;
int modoOperacion  = 0;
int temaEstetico   = 0;
int cursorPos      = 0;

bool enSubmenuSlot = false;
int  cursorSubmenu = 0;

int  animacionActiva   = 0;
int  animFrameCounter  = 0;
bool animToggle        = false;
bool enEmergencia      = false;   

struct ColoresTema {
  uint16_t fondo, panel, textoPrincipal, textoAcento, borde, activoON, activoOFF;
};
ColoresTema t;

void actualizarPaletaColores() {
  if      (temaEstetico == 0) t = { ILI9341_BLACK, 0x1000, ILI9341_WHITE, 0xF800, 0xF800, ILI9341_RED,   0x3186 };
  else if (temaEstetico == 1) t = { 0x2124, 0x4208, ILI9341_WHITE, 0x07E0, 0x8410, 0x07E0, 0xF800 };
  else if (temaEstetico == 2) t = { 0xCE79, 0xBDF7, 0x0000,        0xFD20, 0x7BEF, 0x03E0, 0x8000 };
  else                        t = { ILI9341_BLACK, 0x0010, ILI9341_WHITE, 0x07FF, 0xF81F, 0x07FF, 0xF81F };
}

float temperaturaActual   = 24.5;
float ultimaTempMostrada  = -999;
float temperaturaObjetivo = 18.0;

int horaActual    = 12, minutoActual = 0, segundoActual = 0;
int horaAlarma    =  7, minutoAlarma = 30;
bool alarmaActiva    = true;
bool alarmaDisparada = false;

// campoSeleccionado: 0=ninguno 1=horaSis 2=minSis 3=horaAlm 4=minAlm
int campoSeleccionado = 0;

unsigned long tiempoUltimoSegundo = 0;
unsigned long ultimaLecturaTemp   = 0;
unsigned long ultimaAnimFrame     = 0;

struct SlotCiclo {
  char  nombreSlot[11];
  float tempMeta;
  unsigned long duracionMinutos;
  bool  relCompresor, relVentilador, relSuministro, relRecirc;
};
SlotCiclo slotsMemoria[5] = {
  { "Enfriar",  15.0, 120, true,  true,  true,  false },
  { "Reposo",   10.0,  60, true,  false, false, false },
  { "Shock",     5.0,  30, true,  true,  false, false },
  { "Manten",   18.0,  90, false, true,  true,  false },
  { "Proceso",  20.0,  60, false, false, true,  true  }
};
int  slotSeleccionadoConfig = 0;
int  slotActivoEjecucion    = 0;
unsigned long tiempoInicioSlot    = 0;
unsigned long tiempoAcumuladoSlot = 0;
bool cicloIniciado = false;
bool cicloPausado  = false;

struct Boton { int x, y, w, h; const char* etiqueta; };

Boton botonesDashboard[] = {
  { 215, 26,  90, 20, "TEMA"       },
  { 125, 95,  45, 20, "SUMINISTRO" },
  { 235, 95,  45, 20, "RECIRC"     },
  { 125,128,  45, 20, "VENTILADOR" },
  { 235,128,  45, 20, "COMPRESOR"  },
  {   0,205,  60, 35, "<"          },
  { 260,205,  60, 35, ">"          },
  {  15,168, 140, 30, "EMERG"      },   
  { 165,168, 140, 30, "PAUSA"      }   
};
int totalBotonesDashboard = 9;

Boton botonesTermostato[] = {
  {  15,120, 135, 40, "-0.5C" },
  { 170,120, 135, 40, "+0.5C" },
  {  15,170, 290, 28, "MODO"  },
  {   0,205,  60, 35, "<"     },
  { 260,205,  60, 35, ">"     }
};
int totalBotonesTermostato = 5;
Boton botonesReloj[] = {
  {  15, 58,  55, 28, "H.SIS"    },  
  {  78, 58,  55, 28, "M.SIS"    },  
  { 168, 58,  52, 28, "AMPM.SIS" },  
  {  15,120,  55, 28, "H.ALM"    }, 
  {  78,120,  55, 28, "M.ALM"    }, 
  { 142,120,  48, 28, "AMPM.ALM" }, 
  { 194,120, 116, 28, "ALM.ON"   }, 
  {   0,205,  60, 35, "<"        },  
  { 260,205,  60, 35, ">"        },  
  {  15,172, 140, 28, "SUBIR"    },  
  { 165,172, 140, 28, "BAJAR"    }   
};
int totalBotonesReloj = 11;

Boton botonesSlots[] = {
  {  15,110,  65, 35, "-TEMP"  },
  {  90,110,  65, 35, "+TEMP"  },
  { 165,110, 140, 35, "CONFIG" },
  {  15,155, 290, 38, "CICLO"  },
  {   0,205,  60, 35, "<"      },
  { 260,205,  60, 35, ">"      }
};
int totalBotonesSlots = 6;

Boton botonesBLE[] = {
  {   0,205, 60, 35, "<" },
  { 260,205, 60, 35, ">" }
};
int totalBotonesBLE = 2;
void apagarTodo();
void refrescarPantallaActual();
void dibujarDashboard();
void dibujarControlTemperatura();
void dibujarRelojAlarma();
void dibujarInterfazSlots();
void dibujarPantallaQR();
void dibujarSubmenuSlot();
void dibujarBarraSuperior();
void ejecutarLogicaModos();
void aplicarReles();
void actualizarValoresDinamicos();
void procesarComandoBLE(String cmd);
void dibujarCursor();
void borrarCursor();
void confirmarBoton();
int  totalBotonesPantallaActual();
Boton botonActual();
void iniciarAnimacion(int tipo);
void tickAnimacion();
void detenerAnimacion();
bool procesarUpDownCampo(bool subir);
String formatoAmPm(int h24, int m, int s, bool conSegundos);
int   hora12(int h24);
bool  esAm(int h24);

int hora12(int h24) { int h=h24%12; return (h==0)?12:h; }
bool esAm(int h24)  { return h24<12; }

String formatoAmPm(int h24, int m, int s, bool conSegundos) {
  char buf[16];
  if (conSegundos)
    sprintf(buf,"%02d:%02d:%02d %s", hora12(h24), m, s, esAm(h24)?"AM":"PM");
  else
    sprintf(buf,"%02d:%02d %s", hora12(h24), m, esAm(h24)?"AM":"PM");
  return String(buf);
}

//ble1
class MyServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer* s)    { dispositivoConectado = true; telPendiente = true; slotsPendiente = true; }
  void onDisconnect(BLEServer* s) { dispositivoConectado = false; s->startAdvertising(); }
};
class MyCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *p) {
    String cmd = p->getValue(); cmd.trim();
    Serial.print("BLE:["); Serial.print(cmd); Serial.println("]");
    encolar(cmd);
  }
};
//supbar
void dibujarBarraSuperior() {
  tft.fillRect(0,0,320,24,0x1082);
  tft.setTextColor(ILI9341_CYAN); tft.setTextSize(1);
  tft.setCursor(4,8);
  tft.print(formatoAmPm(horaActual,minutoActual,segundoActual,true));

  if (alarmaActiva) {
    tft.setTextColor(alarmaDisparada ? ILI9341_YELLOW : 0x8410);
    tft.setCursor(110,8); tft.print("ALM ");
    tft.print(formatoAmPm(horaAlarma,minutoAlarma,0,false));
  }

  tft.setCursor(220,8);
  if      (modoOperacion==0) { tft.setTextColor(0x8410);         tft.print("MAN"); }
  else if (modoOperacion==1) { tft.setTextColor(ILI9341_GREEN);  tft.print("TER"); }
  else                       { tft.setTextColor(ILI9341_YELLOW); tft.print("CIC"); }

  tft.setTextColor(ILI9341_WHITE); tft.setCursor(250,8);
  tft.print(temperaturaActual,1); tft.print("C");

  tft.setCursor(300,8);
  if (dispositivoConectado) { tft.setTextColor(ILI9341_BLUE); tft.print("B"); }
  else                      { tft.setTextColor(0x8410);        tft.print("-"); }

  tft.drawFastHLine(0,24,320,t.borde);
}

//animaciones
void iniciarAnimacion(int tipo) {
  animacionActiva=tipo; animFrameCounter=0; animToggle=false;
  apagarTodo();
  tft.fillScreen(ILI9341_BLACK);
}

void tickAnimacion() {
  if (animacionActiva==0) return;
  animFrameCounter++; animToggle=!animToggle;

  if (animacionActiva==1) {   // ALARMA
    uint16_t col1 = animToggle ? ILI9341_RED    : ILI9341_BLACK;
    uint16_t col2 = animToggle ? ILI9341_YELLOW : ILI9341_RED;
    tft.fillScreen(col1); dibujarBarraSuperior();
    tft.setTextColor(col2); tft.setTextSize(3);
    tft.setCursor(30,50);  tft.print("! ALARMA !");
    tft.setCursor(30,90);  tft.print("! ALARMA !");
    tft.setTextSize(2); tft.setTextColor(ILI9341_WHITE);
    tft.setCursor(70,135);
    tft.print(formatoAmPm(horaAlarma,minutoAlarma,0,false));
    tft.setTextSize(1); tft.setTextColor(ILI9341_CYAN);
    tft.setCursor(35,178); tft.print("Presiona  HORN  para apagar");
    if (animToggle) { digitalWrite(RELAY_VENTILADOR,HIGH); digitalWrite(RELAY_RECIRC,LOW);  }
    else            { digitalWrite(RELAY_VENTILADOR,LOW);  digitalWrite(RELAY_RECIRC,HIGH); }
    if (animFrameCounter>=30) detenerAnimacion();
    return;
  }
//finciclo
  if (animacionActiva==2) {  
    uint16_t cols[3]={0xF81F,0x001F,0x07E0};
    uint16_t colFondo=cols[animFrameCounter%3];
    uint16_t colTexto=animToggle?ILI9341_WHITE:ILI9341_YELLOW;
    tft.fillScreen(colFondo); dibujarBarraSuperior();
    tft.setTextSize(2); tft.setTextColor(colTexto);
    tft.setCursor(18,50);  tft.print("CICLO COMPLETO");
    tft.setCursor(18,78);  tft.print("==============");
    tft.setTextSize(1); tft.setTextColor(ILI9341_WHITE);
    tft.setCursor(30,112); tft.print("Ultimo slot: ");
    tft.print(slotsMemoria[slotActivoEjecucion].nombreSlot);
    for (int i=0;i<5;i++) {
      uint16_t c=animToggle?ILI9341_YELLOW:ILI9341_WHITE;
      tft.fillRect(20+i*58,138,48,10+(i%3)*8,c);
    }
    tft.setTextSize(1); tft.setTextColor(ILI9341_WHITE);
    tft.setCursor(30,180); tft.print("Presiona  HORN  para continuar");
    if (animFrameCounter<=6 && animToggle) {
      digitalWrite(RELAY_COMPRESOR,HIGH); digitalWrite(RELAY_VENTILADOR,HIGH);
      delay(150);
      digitalWrite(RELAY_COMPRESOR,LOW);  digitalWrite(RELAY_VENTILADOR,LOW);
    }
    if (animFrameCounter>=24) detenerAnimacion();
  }
}

void detenerAnimacion() {
  animacionActiva=0; animFrameCounter=0; alarmaDisparada=false;
  digitalWrite(RELAY_VENTILADOR,LOW); digitalWrite(RELAY_RECIRC,LOW);
  digitalWrite(RELAY_COMPRESOR,LOW);  digitalWrite(RELAY_SUMINISTRO,LOW);
  refrescarPantallaActual();
}

//cursor
int totalBotonesPantallaActual() {
  if (enSubmenuSlot)       return 7;
  if (pantallaActual == 0) return totalBotonesDashboard;
  if (pantallaActual == 1) return totalBotonesTermostato;
  if (pantallaActual == 2) return totalBotonesReloj;
  if (pantallaActual == 3) return totalBotonesSlots;
  return totalBotonesBLE;
}

Boton botonActual() {
  if (enSubmenuSlot) {
    Boton sub[] = {
      {  15, 55,290,25,"TEMP"       },
      {  15, 85,290,25,"DURACION"   },
      {  15,115,290,25,"COMPRESOR"  },
      {  15,145,290,25,"VENTILADOR" },
      {  15,175,290,25,"SUMINISTRO" },
      {  15,205,145,25,"RECIRC"     },
      { 165,205,140,25,"GUARDAR"    }
    };
    return sub[cursorSubmenu];
  }
  if (pantallaActual==0) return botonesDashboard[cursorPos];
  if (pantallaActual==1) return botonesTermostato[cursorPos];
  if (pantallaActual==2) return botonesReloj[cursorPos];
  if (pantallaActual==3) return botonesSlots[cursorPos];
  return botonesBLE[cursorPos];
}

void dibujarCursor() {
  Boton b=botonActual();
  tft.drawRect(b.x-2,b.y-2,b.w+4,b.h+4,ILI9341_CYAN);
  tft.drawRect(b.x-3,b.y-3,b.w+6,b.h+6,ILI9341_CYAN);
}
void borrarCursor() {
  Boton b=botonActual();
  tft.drawRect(b.x-2,b.y-2,b.w+4,b.h+4,t.fondo);
  tft.drawRect(b.x-3,b.y-3,b.w+6,b.h+6,t.fondo);
}
void flashVerde() {
  Boton b=botonActual();
  tft.drawRect(b.x,b.y,b.w,b.h,ILI9341_GREEN); delay(80);
  tft.drawRect(b.x,b.y,b.w,b.h,t.borde);
}

//slot.submenu
void dibujarSubmenuSlot() {
  actualizarPaletaColores();
  tft.fillScreen(t.fondo);
  dibujarBarraSuperior();

  SlotCiclo &s=slotsMemoria[slotSeleccionadoConfig];
  tft.setTextColor(t.textoAcento); tft.setTextSize(1);
  tft.setCursor(10,28); tft.print("CONFIG SLOT ");
  tft.print(slotSeleccionadoConfig+1); tft.print(": "); tft.print(s.nombreSlot);

  char v0[12],v1[12];
  sprintf(v0,"%.1f C",s.tempMeta);
  sprintf(v1,"%lu min",s.duracionMinutos);

  struct F { int y; const char* lbl; const char* val; const char* hint; uint16_t vc; };
  F filas[5]={
    { 55,"Temp meta:  ",v0,"[L/R]",t.textoAcento},
    { 85,"Duracion:   ",v1,"[L/R]",t.textoAcento},
    {115,"Compresor:  ",s.relCompresor ?"ON ":"OFF","[A]",s.relCompresor ?t.activoON:t.activoOFF},
    {145,"Ventilador: ",s.relVentilador?"ON ":"OFF","[A]",s.relVentilador?t.activoON:t.activoOFF},
    {175,"Suministro: ",s.relSuministro?"ON ":"OFF","[A]",s.relSuministro?t.activoON:t.activoOFF}
  };
  for (int i=0;i<5;i++){
    tft.fillRect(15,filas[i].y,290,25,t.panel); tft.drawRect(15,filas[i].y,290,25,t.borde);
    tft.setTextColor(t.textoPrincipal); tft.setCursor(20,filas[i].y+8); tft.print(filas[i].lbl);
    tft.setTextColor(filas[i].vc);      tft.print(filas[i].val);
    tft.setTextColor(0x8410);           tft.setCursor(245,filas[i].y+8); tft.print(filas[i].hint);
  }
  tft.fillRect( 15,205,145,25,t.panel);   tft.drawRect( 15,205,145,25,t.borde);
  tft.setTextColor(t.textoPrincipal); tft.setCursor(20,213); tft.print("Recirc: ");
  tft.setTextColor(s.relRecirc?t.activoON:t.activoOFF); tft.print(s.relRecirc?"ON":"OFF");

  tft.fillRect(165,205,140,25,t.activoON); tft.drawRect(165,205,140,25,t.borde);
  tft.setTextColor(ILI9341_WHITE); tft.setCursor(195,213); tft.print("GUARDAR [A]");
  dibujarCursor();
}

// a
void confirmarBoton() {
  flashVerde();

  if (enSubmenuSlot) {
    SlotCiclo &s=slotsMemoria[slotSeleccionadoConfig];
    switch(cursorSubmenu){
      case 2: s.relCompresor =!s.relCompresor;  dibujarSubmenuSlot(); return;
      case 3: s.relVentilador=!s.relVentilador; dibujarSubmenuSlot(); return;
      case 4: s.relSuministro=!s.relSuministro; dibujarSubmenuSlot(); return;
      case 5: s.relRecirc    =!s.relRecirc;     dibujarSubmenuSlot(); return;
      case 6: enSubmenuSlot=false; cursorPos=0; cursorSubmenu=0; refrescarPantallaActual(); return;
      default: dibujarCursor(); return;
    }
  }

  if (pantallaActual==0) {
    if (cursorPos==0){temaEstetico=(temaEstetico+1)%4; refrescarPantallaActual(); return;}
    if (modoOperacion==0){
      if (cursorPos==1){estadoSuministro=!estadoSuministro; aplicarReles(); dibujarDashboard(); dibujarCursor(); return;}
      if (cursorPos==2){estadoRecirc    =!estadoRecirc;     aplicarReles(); dibujarDashboard(); dibujarCursor(); return;}
      if (cursorPos==3){estadoVentilador=!estadoVentilador; aplicarReles(); dibujarDashboard(); dibujarCursor(); return;}
      if (cursorPos==4){estadoCompresor =!estadoCompresor;  aplicarReles(); dibujarDashboard(); dibujarCursor(); return;}
    }
    if (cursorPos==5){pantallaActual=(pantallaActual==0)?4:pantallaActual-1; cursorPos=0; refrescarPantallaActual(); return;}
    if (cursorPos==6){pantallaActual=(pantallaActual+1)%5; cursorPos=0; refrescarPantallaActual(); return;}
    if (cursorPos==7){procesarComandoBLE("LIGHT"); return;}
    if (cursorPos==8){procesarComandoBLE("HORN");  return;}
  }
  else if (pantallaActual==1) {
    if (cursorPos==0&&temperaturaObjetivo>2.0) {temperaturaObjetivo-=0.5; dibujarControlTemperatura(); dibujarCursor(); return;}
    if (cursorPos==1&&temperaturaObjetivo<40.0){temperaturaObjetivo+=0.5; dibujarControlTemperatura(); dibujarCursor(); return;}
    if (cursorPos==2){modoOperacion=(modoOperacion==1)?0:1; dibujarControlTemperatura(); dibujarCursor(); return;}
    if (cursorPos==3){pantallaActual--; if(pantallaActual<0)pantallaActual=4; cursorPos=0; refrescarPantallaActual(); return;}
    if (cursorPos==4){pantallaActual=(pantallaActual+1)%5; cursorPos=0; refrescarPantallaActual(); return;}
  }
  else if (pantallaActual==2) {
    if (cursorPos==0){campoSeleccionado=(campoSeleccionado==1)?0:1; dibujarRelojAlarma(); dibujarCursor(); return;}
    if (cursorPos==1){campoSeleccionado=(campoSeleccionado==2)?0:2; dibujarRelojAlarma(); dibujarCursor(); return;}
    if (cursorPos==2){
      if(horaActual<12) horaActual+=12; else horaActual-=12;
      dibujarRelojAlarma(); dibujarCursor(); return;
    }
    if (cursorPos==3){campoSeleccionado=(campoSeleccionado==3)?0:3; dibujarRelojAlarma(); dibujarCursor(); return;}
    if (cursorPos==4){campoSeleccionado=(campoSeleccionado==4)?0:4; dibujarRelojAlarma(); dibujarCursor(); return;}
    if (cursorPos==5){
      if(horaAlarma<12) horaAlarma+=12; else horaAlarma-=12;
      dibujarRelojAlarma(); dibujarCursor(); return;
    }
    if (cursorPos==6){alarmaActiva=!alarmaActiva; dibujarRelojAlarma(); dibujarCursor(); return;}
    if (cursorPos==7){pantallaActual--; if(pantallaActual<0)pantallaActual=4; cursorPos=0; campoSeleccionado=0; refrescarPantallaActual(); return;}
    if (cursorPos==8){pantallaActual=(pantallaActual+1)%5; cursorPos=0; campoSeleccionado=0; refrescarPantallaActual(); return;}
    if (cursorPos==9) { procesarUpDownCampo(true);  return; }
    if (cursorPos==10){ procesarUpDownCampo(false); return; }
  }
  else if (pantallaActual==3) {
    if (cursorPos==0&&slotsMemoria[slotSeleccionadoConfig].tempMeta>2.0) {slotsMemoria[slotSeleccionadoConfig].tempMeta-=0.5; dibujarInterfazSlots(); dibujarCursor(); return;}
    if (cursorPos==1&&slotsMemoria[slotSeleccionadoConfig].tempMeta<40.0){slotsMemoria[slotSeleccionadoConfig].tempMeta+=0.5; dibujarInterfazSlots(); dibujarCursor(); return;}
    if (cursorPos==2){enSubmenuSlot=true; cursorSubmenu=0; dibujarSubmenuSlot(); return;}
    if (cursorPos==3){
      if(!cicloIniciado){
        cicloIniciado=true; cicloPausado=false;
        slotActivoEjecucion=slotSeleccionadoConfig;
        tiempoInicioSlot=millis(); tiempoAcumuladoSlot=0; modoOperacion=2;
      } else { cicloIniciado=false; cicloPausado=false; apagarTodo(); }
      dibujarInterfazSlots(); dibujarCursor(); return;
    }
    if (cursorPos==4){pantallaActual--; if(pantallaActual<0)pantallaActual=4; cursorPos=0; refrescarPantallaActual(); return;}
    if (cursorPos==5){pantallaActual=(pantallaActual+1)%5; cursorPos=0; refrescarPantallaActual(); return;}
  }
  else if (pantallaActual==4) {
    if (cursorPos==0){pantallaActual=3; cursorPos=0; refrescarPantallaActual(); return;}
    if (cursorPos==1){pantallaActual=0; cursorPos=0; refrescarPantallaActual(); return;}
  }
}

// ══════════════════════════════════════════════════════════
// UP/DOWN sobre campo activo
// ══════════════════════════════════════════════════════════
bool procesarUpDownCampo(bool subir) {
  if (campoSeleccionado==0) return false;
  if (campoSeleccionado==1){
    horaActual=subir?(horaActual+1)%24:(horaActual==0?23:horaActual-1);
    segundoActual=0; dibujarRelojAlarma(); dibujarCursor(); return true;
  }
  if (campoSeleccionado==2){
    minutoActual=subir?(minutoActual+1)%60:(minutoActual==0?59:minutoActual-1);
    segundoActual=0; dibujarRelojAlarma(); dibujarCursor(); return true;
  }
  if (campoSeleccionado==3){
    horaAlarma=subir?(horaAlarma+1)%24:(horaAlarma==0?23:horaAlarma-1);
    dibujarRelojAlarma(); dibujarCursor(); return true;
  }
  if (campoSeleccionado==4){
    minutoAlarma=subir?(minutoAlarma+1)%60:(minutoAlarma==0?59:minutoAlarma-1);
    dibujarRelojAlarma(); dibujarCursor(); return true;
  }
  return false;
}

//comandosble
void procesarComandoBLE(String cmd) {

  //horn
  if (cmd == "HORN") {
    if (enEmergencia) {                          
      enEmergencia = false;
      refrescarPantallaActual();
      return;
    }
    if (animacionActiva) {                      
      detenerAnimacion();
      return;
    }
    if (cicloIniciado && !cicloPausado) {      
      tiempoAcumuladoSlot = millis() - tiempoInicioSlot;
      cicloPausado=true; apagarTodo(); modoOperacion=0;
      Serial.println("Ciclo pausado.");
      refrescarPantallaActual();
      return;
    }
    if (cicloIniciado && cicloPausado) {        
      tiempoInicioSlot = millis() - tiempoAcumuladoSlot;
      cicloPausado=false; modoOperacion=2;
      Serial.println("Ciclo reanudado.");
      refrescarPantallaActual();
      return;
    }
    return;
  }

  //paroemergencia
  if (cmd == "LIGHT") {
    apagarTodo(); enSubmenuSlot=false; animacionActiva=0; enEmergencia=true;
    tft.fillScreen(ILI9341_RED);
    dibujarBarraSuperior();
    tft.setTextColor(ILI9341_WHITE); tft.setTextSize(3);
    tft.setCursor(10,60);  tft.print("EMERGENCIA");
    tft.setCursor(10,100); tft.print("DETENIDO");
    tft.setTextSize(1); tft.setCursor(30,155);
    tft.print("Todo apagado. Toca o HORN para volver.");
    Serial.println("!!! PARO DE EMERGENCIA !!!");
    return;
  }

  if (cmd == "D") return;
  if (enEmergencia || animacionActiva) return;

  //comandosapp
  if (cmd.startsWith("PAN:")) {
    int n = cmd.substring(4).toInt();
    if (n>=0 && n<=4) { enSubmenuSlot=false; cursorPos=0; campoSeleccionado=0; pantallaActual=n; refrescarPantallaActual(); }
    return;
  }
  if (cmd.startsWith("BTN:")) {
    int i = cmd.substring(4).toInt();
    if (i<0 || i>=totalBotonesPantallaActual()) return;
    borrarCursor();
    if (enSubmenuSlot) cursorSubmenu=i; else cursorPos=i;
    dibujarCursor(); confirmarBoton(); return;
  }
  if (cmd.startsWith("CFG:")) {
    int i = cmd.substring(4).toInt();
    if (i>=0 && i<5) { slotSeleccionadoConfig=i; if (pantallaActual==3 || enSubmenuSlot) refrescarPantallaActual(); }
    return;
  }
  if (cmd.startsWith("SLOT:")) {         
    int i, d; float tt; char m[8] = {0};
    if (sscanf(cmd.c_str()+5, "%d,%f,%d,%4s", &i, &tt, &d, m)==4 && i>=0 && i<5 && strlen(m)==4) {
      SlotCiclo &sl = slotsMemoria[i];
      sl.tempMeta = constrain(tt, 2.0, 40.0);
      sl.duracionMinutos = constrain(d, 5, 480);
      sl.relSuministro = (m[0]=='1'); sl.relRecirc = (m[1]=='1');
      sl.relVentilador = (m[2]=='1'); sl.relCompresor = (m[3]=='1');
      slotsPendiente = true;
      if (pantallaActual==3 || enSubmenuSlot) refrescarPantallaActual();
    }
    return;
  }
//(pinky)UP
  if (cmd == "UP") {
    if (enSubmenuSlot){borrarCursor(); cursorSubmenu=(cursorSubmenu==0)?6:cursorSubmenu-1; dibujarCursor(); return;}
    if (pantallaActual==2 && procesarUpDownCampo(true)) return;
    borrarCursor(); cursorPos=(cursorPos==0)?totalBotonesPantallaActual()-1:cursorPos-1; dibujarCursor(); return;
  }

  //down
  if (cmd == "DOWN") {
    if (enSubmenuSlot){borrarCursor(); cursorSubmenu=(cursorSubmenu+1)%7; dibujarCursor(); return;}
    if (pantallaActual==2 && procesarUpDownCampo(false)) return;
    borrarCursor(); cursorPos=(cursorPos+1)%totalBotonesPantallaActual(); dibujarCursor(); return;
  }
//left
  if (cmd == "LEFT") {
    if (enSubmenuSlot){
      SlotCiclo &s=slotsMemoria[slotSeleccionadoConfig];
      if (cursorSubmenu==0&&s.tempMeta>2.0)     {s.tempMeta-=0.5;   dibujarSubmenuSlot(); return;}
      if (cursorSubmenu==1&&s.duracionMinutos>5) {s.duracionMinutos-=5; dibujarSubmenuSlot(); return;}
      return;
    }
    if (pantallaActual==3){slotSeleccionadoConfig=(slotSeleccionadoConfig==0)?4:slotSeleccionadoConfig-1; dibujarInterfazSlots(); dibujarCursor(); return;}
    borrarCursor(); cursorPos=(cursorPos==0)?totalBotonesPantallaActual()-1:cursorPos-1; dibujarCursor(); return;
  }
//right
  if (cmd == "RIGHT") {
    if (enSubmenuSlot){
      SlotCiclo &s=slotsMemoria[slotSeleccionadoConfig];
      if (cursorSubmenu==0&&s.tempMeta<40.0)      {s.tempMeta+=0.5;   dibujarSubmenuSlot(); return;}
      if (cursorSubmenu==1&&s.duracionMinutos<480) {s.duracionMinutos+=5; dibujarSubmenuSlot(); return;}
      return;
    }
    if (pantallaActual==3){slotSeleccionadoConfig=(slotSeleccionadoConfig+1)%5; dibujarInterfazSlots(); dibujarCursor(); return;}
    borrarCursor(); cursorPos=(cursorPos+1)%totalBotonesPantallaActual(); dibujarCursor(); return;
  }

//a
  if (cmd == "A") { confirmarBoton(); return; }
//b
  if (cmd == "B") {
    if (enSubmenuSlot){enSubmenuSlot=false; cursorSubmenu=0; cursorPos=0; refrescarPantallaActual(); return;}
    borrarCursor(); cursorPos=0; campoSeleccionado=0;
    pantallaActual=(pantallaActual==0)?4:pantallaActual-1;
    refrescarPantallaActual(); return;
  }

//c
  if (cmd == "C") {
    if (enSubmenuSlot){enSubmenuSlot=false; cursorSubmenu=0; cursorPos=0; refrescarPantallaActual(); return;}
    borrarCursor(); cursorPos=0; campoSeleccionado=0;
    pantallaActual=(pantallaActual+1)%5;
    refrescarPantallaActual(); return;
  }
}

//setup
void setup() {
  Serial.begin(115200);
  SPI.begin(SPI_SCK,SPI_MISO,SPI_MOSI);
  tft.begin(); tft.setRotation(3);
  if (USAR_TOUCH) { ts.begin(); ts.setRotation(3); }
  sensors.begin();
  sensors.setWaitForConversion(false);   //sinbloquear
  sensors.requestTemperatures();

  pinMode(RELAY_SUMINISTRO,OUTPUT); pinMode(RELAY_RECIRC,    OUTPUT);
  pinMode(RELAY_VENTILADOR,OUTPUT); pinMode(RELAY_COMPRESOR, OUTPUT);
  apagarTodo();

  BLEDevice::init("TermostatoGame");
  BLEDevice::setMTU(185);
  BLEServer *pServer=BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());
  BLEService *pService=pServer->createService(SERVICE_UUID);
  pCharacteristic=pService->createCharacteristic(CHARACTERISTIC_UUID_TX,BLECharacteristic::PROPERTY_NOTIFY);
  pCharacteristic->addDescriptor(new BLE2902());
  BLECharacteristic *pRx=pService->createCharacteristic(CHARACTERISTIC_UUID_RX,BLECharacteristic::PROPERTY_WRITE);
  pRx->setCallbacks(new MyCallbacks());
  pService->start(); pServer->startAdvertising();

  actualizarPaletaColores();
  tft.fillScreen(t.fondo);
  refrescarPantallaActual();
  tiempoUltimoSegundo=millis();
  Serial.println("Sistema listo.");
}
//loop
void loop() {
  procesarCola();
  if (USAR_TOUCH) leerTouch();

  if (millis()-tiempoUltimoSegundo>=1000) {
    tiempoUltimoSegundo+=1000;
    if (campoSeleccionado==0 && !enEmergencia) {
      segundoActual++;
      if (segundoActual>=60){
        segundoActual=0; minutoActual++;
        if (minutoActual>=60){minutoActual=0; horaActual=(horaActual+1)%24;}
      }
    }
    if (!animacionActiva && !enEmergencia) dibujarBarraSuperior();
    if (alarmaActiva && !alarmaDisparada &&
        horaActual==horaAlarma && minutoActual==minutoAlarma && segundoActual==0) {
      alarmaDisparada=true;
      iniciarAnimacion(1);
    }
  }
  if (animacionActiva && millis()-ultimaAnimFrame>=250) {
    ultimaAnimFrame=millis(); tickAnimacion();
  }
  if (millis()-ultimaLecturaTemp>2000) {
    ultimaLecturaTemp=millis();
    float tmp=sensors.getTempCByIndex(0);   // conversion pedida en el ciclo anterior
    sensors.requestTemperatures();
    if (tmp!=-127.0&&tmp!=85.0) temperaturaActual=tmp;
    if (!animacionActiva && !enEmergencia) { actualizarValoresDinamicos(); ejecutarLogicaModos(); }
    telPendiente=true;
  }
// Telemetria hacia la app
  if (dispositivoConectado && (telPendiente||slotsPendiente) && millis()-ultimaTel>150) {
    ultimaTel=millis();
    if (slotsPendiente){ slotsPendiente=false; enviarSlotsBLE(); }
    if (telPendiente)  { telPendiente=false;  enviarTelemetria(); }
  }
}
//comandos
void procesarCola() {
  while (qT != qH) {
    String c = colaCmd[qT]; qT = (qT + 1) % QN;
    procesarComandoBLE(c);
    telPendiente = true;
  }
}

//ble en 20 bytes
void notificar(const char* txt) {
  size_t n = strlen(txt);
  for (size_t i = 0; i < n; i += 20) {
    size_t l = (n - i < 20) ? (n - i) : 20;
    pCharacteristic->setValue((uint8_t*)(txt + i), l);
    pCharacteristic->notify();
    delay(8);
  }
}
void enviarTelemetria() {
  char b[160];
  snprintf(b, sizeof(b),
    "Temp:%.1f|Meta:%.1f|Modo:%d|Slot:%d|Ciclo:%d|Sumi:%d|Recirc:%d|Vent:%d|Comp:%d|Pan:%d|Pau:%d|Cur:%d|Em:%d|Cfg:%d\n",
    temperaturaActual, temperaturaObjetivo, modoOperacion, slotActivoEjecucion, cicloIniciado?1:0,
    estadoSuministro?1:0, estadoRecirc?1:0, estadoVentilador?1:0, estadoCompresor?1:0,
    pantallaActual, cicloPausado?1:0, enSubmenuSlot?cursorSubmenu:cursorPos, enEmergencia?1:0,
    slotSeleccionadoConfig);
  notificar(b);
}
void enviarSlotsBLE() {
  char b[200]; int n = 0;
  for (int i = 0; i < 5; i++) {
    SlotCiclo &sl = slotsMemoria[i];
    n += snprintf(b+n, sizeof(b)-n, "S%d:%.1f,%lu,%d%d%d%d|", i, sl.tempMeta, sl.duracionMinutos,
                  sl.relSuministro?1:0, sl.relRecirc?1:0, sl.relVentilador?1:0, sl.relCompresor?1:0);
  }
  snprintf(b+n, sizeof(b)-n, "Cfg:%d\n", slotSeleccionadoConfig);
  notificar(b);
}
// touchxtoque con cordenadas de boton
void leerTouch() {
  static bool tocando = false;
  static unsigned long ultimo = 0;
  if (!ts.touched()) { tocando = false; return; }
  if (tocando || millis() - ultimo < 120) return;  
  tocando = true; ultimo = millis();
  TS_Point p = ts.getPoint();
  int x = constrain(map(p.x, TS_XMIN, TS_XMAX, 0, 320), 0, 319);
  int y = constrain(map(p.y, TS_YMIN, TS_YMAX, 0, 240), 0, 239);
  Serial.printf("TOUCH raw=%d,%d -> %d,%d\n", p.x, p.y, x, y);
  telPendiente = true;
// horn
  if (enEmergencia || animacionActiva) { procesarComandoBLE("HORN"); return; }
// Slots
  if (pantallaActual == 3 && !enSubmenuSlot && y >= 44 && y <= 94 && x >= 15) {
    int i = (x - 15) / 58;
    if (i >= 0 && i < 5) { slotSeleccionadoConfig = i; dibujarInterfazSlots(); dibujarCursor(); return; }
  }
  int &cur = enSubmenuSlot ? cursorSubmenu : cursorPos;
  int viejo = cur, total = totalBotonesPantallaActual();
  for (int i = 0; i < total; i++) {
    cur = i;
    Boton b = botonActual();
    if (x >= b.x-4 && x <= b.x+b.w+4 && y >= b.y-4 && y <= b.y+b.h+4) {
      // En el submenu, TEMP y DURACION: mitad izquierda = menos, derecha = mas
      if (enSubmenuSlot && i < 2) { procesarComandoBLE(x < 160 ? "LEFT" : "RIGHT"); return; }
      cur = viejo; borrarCursor();
      cur = i;     dibujarCursor();
      confirmarBoton();
      return;
    }
  }
  cur = viejo;
}

//refresh
void refrescarPantallaActual() {
  if (enSubmenuSlot){dibujarSubmenuSlot(); return;}
  actualizarPaletaColores();
  tft.fillScreen(t.fondo);
  dibujarBarraSuperior();
  tft.fillRect(  0,205,60,35,t.panel); tft.drawRect(  0,205,60,35,t.borde);
  tft.setTextColor(t.textoPrincipal); tft.setTextSize(2); tft.setCursor(20,213); tft.print("<");
  tft.fillRect(260,205,60,35,t.panel); tft.drawRect(260,205,60,35,t.borde);
  tft.setCursor(280,213); tft.print(">");
  tft.fillRect(61,205,198,35,t.fondo);
  tft.setTextSize(1); tft.setTextColor(t.textoAcento); tft.setCursor(72,216);
  const char* nombres[]={"[DASHBOARD]","[TERMOSTATO]","[RELOJ & ALARMA]","[SLOTS]","[BLUETOOTH]"};
  tft.print(nombres[pantallaActual]);

  switch(pantallaActual){
    case 0: dibujarDashboard();          break;
    case 1: dibujarControlTemperatura(); break;
    case 2: dibujarRelojAlarma();        break;
    case 3: dibujarInterfazSlots();      break;
    case 4: dibujarPantallaQR();         break;
  }
  ultimaTempMostrada=-999;
  dibujarCursor();
}

//pantallas
void dibujarDashboard() {
  tft.setTextColor(t.textoAcento); tft.setTextSize(1);
  tft.setCursor(15,28); tft.print("CONTROL ESTETICO");
  tft.fillRect(215,26,90,20,t.panel); tft.drawRect(215,26,90,20,t.borde);
  tft.setTextColor(t.textoPrincipal); tft.setCursor(222,32); tft.print("TEMA ESTILO");

  tft.fillRect(15,50,290,26,t.panel); tft.drawRect(15,50,290,26,t.borde);
  tft.setTextColor(t.textoPrincipal); tft.setTextSize(1);
  tft.setCursor(25,58); tft.print("Sensor:");
  tft.setTextSize(2); tft.setTextColor(t.textoAcento);
  tft.setCursor(80,53); tft.print(temperaturaActual,1); tft.print("C");
  tft.setTextSize(1); tft.setTextColor(t.textoPrincipal);
  tft.setCursor(180,58); tft.print("Meta:"); tft.setTextColor(t.textoAcento);
  tft.print(temperaturaObjetivo,1); tft.print("C");
  ultimaTempMostrada=temperaturaActual;

  tft.setTextSize(1); tft.setCursor(15,82);
  if      (modoOperacion==0){tft.setTextColor(0x8410);        tft.print("MODO: MANUAL");}
  else if (modoOperacion==1){tft.setTextColor(t.activoON);    tft.print("MODO: TERMOSTATO");}
  else                      {tft.setTextColor(ILI9341_YELLOW);tft.print("MODO: CICLO AUTO");}
  if (cicloPausado){tft.setTextColor(0xFD20); tft.print(" [PAUSADO]");}

  tft.fillRect(15,92,290,70,t.panel); tft.drawRect(15,92,290,70,t.borde);
  auto indRele=[&](int lx,int ly,const char* lbl,bool est,int bx,int by){
    tft.setTextColor(t.textoPrincipal); tft.setCursor(lx,ly); tft.print(lbl);
    tft.fillRect(bx,by,45,20,est?t.activoON:t.activoOFF); tft.drawRect(bx,by,45,20,t.borde);
    tft.setTextColor(t.textoPrincipal); tft.setCursor(bx+10,by+6); tft.print(est?"ON":"OFF");
  };
  indRele( 25, 99,"Sumi:",estadoSuministro,125, 95);
  indRele(185, 99,"Rec:", estadoRecirc,    235, 95);
  indRele( 25,132,"Vent:",estadoVentilador,125,128);
  indRele(185,132,"Comp:",estadoCompresor, 235,128);

  tft.fillRect( 15,168,140,30,0xF800); tft.drawRect( 15,168,140,30,t.borde);
  tft.setTextSize(1); tft.setTextColor(ILI9341_WHITE); tft.setCursor(40,180); tft.print("EMERGENCIA");
  tft.fillRect(165,168,140,30,t.panel); tft.drawRect(165,168,140,30,t.borde);
  tft.setTextColor(t.textoPrincipal); tft.setCursor(185,180); tft.print("PAUSA/REANUDA");
}

void actualizarValoresDinamicos() {
  if (pantallaActual==0&&!enSubmenuSlot&&!animacionActiva&&!enEmergencia&&
      temperaturaActual!=ultimaTempMostrada){
    tft.fillRect(80,53,95,18,t.panel);
    tft.setTextSize(2); tft.setTextColor(t.textoAcento);
    tft.setCursor(80,53); tft.print(temperaturaActual,1); tft.print("C");
    ultimaTempMostrada=temperaturaActual;
    dibujarBarraSuperior();
  }
}

void dibujarControlTemperatura() {
  tft.setTextColor(t.textoAcento); tft.setTextSize(1);
  tft.setCursor(15,30); tft.print("TERMOSTATO Y SETPOINT");
  tft.fillRect(15,45,290,60,t.panel); tft.drawRect(15,45,290,60,t.borde);
  tft.setTextColor(t.textoPrincipal); tft.setCursor(25,55); tft.print("Actual:");
  tft.setTextSize(2); tft.setTextColor(t.textoAcento);
  tft.setCursor(25,72); tft.print(temperaturaActual,1); tft.print(" C");
  tft.setTextSize(1); tft.setTextColor(t.textoPrincipal);
  tft.setCursor(160,55); tft.print("Meta:");
  tft.setTextSize(2); tft.setTextColor(t.textoAcento);
  tft.setCursor(160,72); tft.print(temperaturaObjetivo,1); tft.print(" C");
  tft.fillRect( 15,120,135,40,t.activoOFF); tft.drawRect( 15,120,135,40,t.borde);
  tft.setTextColor(t.textoPrincipal); tft.setTextSize(1); tft.setCursor(45,135); tft.print("- 0.5 C");
  tft.fillRect(170,120,135,40,t.activoON);  tft.drawRect(170,120,135,40,t.borde);
  tft.setCursor(205,135); tft.print("+ 0.5 C");
  tft.fillRect(15,170,290,28,t.panel); tft.drawRect(15,170,290,28,t.borde);
  tft.setCursor(25,180); tft.print("Modo Termostato: ");
  tft.setTextColor(modoOperacion==1?t.activoON:t.activoOFF);
  tft.print(modoOperacion==1?"ACTIVADO":"APAGADO");
}

void dibujarRelojAlarma() {
  tft.setTextColor(t.textoAcento); tft.setTextSize(1);
  tft.setCursor(8,28); tft.print("RELOJ & ALARMA  [A=campo  UP/DOWN=valor]");

  // Bloque hora sistema
  tft.fillRect(0,44,320,52,t.panel); tft.drawRect(0,44,320,52,t.borde);
  tft.setTextColor(t.textoPrincipal); tft.setCursor(5,50); tft.print("SISTEMA:");

  bool hSisAct=(campoSeleccionado==1), mSisAct=(campoSeleccionado==2);
  tft.fillRect(15,58,55,28,t.panel);
  tft.drawRect(15,58,55,28,hSisAct?ILI9341_YELLOW:t.borde);
  tft.setTextSize(2); tft.setTextColor(hSisAct?ILI9341_YELLOW:t.textoAcento);
  tft.setCursor(18,64); if(hora12(horaActual)<10)tft.print("0"); tft.print(hora12(horaActual));

  tft.setTextColor(t.textoAcento); tft.setCursor(72,64); tft.print(":");

  tft.fillRect(78,58,55,28,t.panel);
  tft.drawRect(78,58,55,28,mSisAct?ILI9341_YELLOW:t.borde);
  tft.setTextSize(2); tft.setTextColor(mSisAct?ILI9341_YELLOW:t.textoAcento);
  tft.setCursor(82,64); if(minutoActual<10)tft.print("0"); tft.print(minutoActual);

  tft.setTextColor(0x8410); tft.setTextSize(1); tft.setCursor(137,70);
  tft.print(":"); if(segundoActual<10)tft.print("0"); tft.print(segundoActual);

  tft.fillRect(168,58,52,28,t.panel); tft.drawRect(168,58,52,28,t.borde);
  tft.setTextSize(2); tft.setTextColor(esAm(horaActual)?ILI9341_CYAN:ILI9341_YELLOW);
  tft.setCursor(172,64); tft.print(esAm(horaActual)?"AM":"PM");
  tft.setTextColor(0x8410); tft.setTextSize(1); tft.setCursor(228,66); tft.print("[A=toggle]");

  // Bloque alarma
  tft.fillRect(0,100,320,64,t.panel); tft.drawRect(0,100,320,64,t.borde);
  tft.setTextColor(t.textoPrincipal); tft.setTextSize(1); tft.setCursor(5,108); tft.print("ALARMA:");

  bool hAlmAct=(campoSeleccionado==3), mAlmAct=(campoSeleccionado==4);
  tft.fillRect(15,120,55,28,t.panel);
  tft.drawRect(15,120,55,28,hAlmAct?ILI9341_YELLOW:t.borde);
  tft.setTextSize(2); tft.setTextColor(hAlmAct?ILI9341_YELLOW:t.textoAcento);
  tft.setCursor(18,126); if(hora12(horaAlarma)<10)tft.print("0"); tft.print(hora12(horaAlarma));

  tft.setTextColor(t.textoAcento); tft.setCursor(72,126); tft.print(":");

  tft.fillRect(78,120,55,28,t.panel);
  tft.drawRect(78,120,55,28,mAlmAct?ILI9341_YELLOW:t.borde);
  tft.setTextSize(2); tft.setTextColor(mAlmAct?ILI9341_YELLOW:t.textoAcento);
  tft.setCursor(82,126); if(minutoAlarma<10)tft.print("0"); tft.print(minutoAlarma);

  tft.setTextSize(2); tft.setTextColor(esAm(horaAlarma)?ILI9341_CYAN:ILI9341_YELLOW);
  tft.setCursor(142,126); tft.print(esAm(horaAlarma)?"AM":"PM");

  tft.fillRect(190,120,120,28,alarmaActiva?t.activoON:t.activoOFF);
  tft.drawRect(190,120,120,28,t.borde);
  tft.setTextSize(1); tft.setTextColor(ILI9341_WHITE);
  tft.setCursor(215,132); tft.print(alarmaActiva?"ALARMA ON":"ALARMA OFF");

  tft.setTextColor(0x8410); tft.setTextSize(1);
  tft.fillRect( 15,172,140,28,t.activoON);  tft.drawRect( 15,172,140,28,t.borde);
  tft.setTextColor(ILI9341_WHITE); tft.setCursor(55,183); tft.print("+ SUBIR");
  tft.fillRect(165,172,140,28,t.activoOFF); tft.drawRect(165,172,140,28,t.borde);
  tft.setCursor(205,183); tft.print("- BAJAR");
}

void dibujarInterfazSlots() {
  tft.setTextColor(t.textoAcento); tft.setTextSize(1);
  tft.setCursor(15,30); tft.print("SLOTS DE PROCESO AUTOMATICO");
  for(int i=0;i<5;i++){
    int xPos=15+i*58;
    uint16_t col=(i==slotSeleccionadoConfig)?t.activoON:t.panel;
    tft.fillRect(xPos,44,52,50,col); tft.drawRect(xPos,44,52,50,t.borde);
    tft.setTextColor(t.textoAcento); tft.setCursor(xPos+14,47); tft.print("#"); tft.print(i+1);
    tft.setTextColor(t.textoPrincipal); tft.setCursor(xPos+3,62);
    String nom=String(slotsMemoria[i].nombreSlot); if(nom.length()>7)nom=nom.substring(0,7);
    tft.print(nom); tft.setCursor(xPos+3,77);
    tft.print((int)slotsMemoria[i].tempMeta); tft.print("C/");
    tft.print(slotsMemoria[i].duracionMinutos); tft.print("m");
  }
  tft.setTextColor(t.textoAcento); tft.setCursor(15,100); tft.print("Slot "); tft.print(slotSeleccionadoConfig+1);
  tft.setTextColor(t.textoPrincipal); tft.setCursor(60,100); tft.print("  [L/R=cambiar]");
  if(cicloIniciado){
    unsigned long transcurrido=(millis()-tiempoInicioSlot+tiempoAcumuladoSlot)/60000;
    unsigned long restante=0;
    if(transcurrido<slotsMemoria[slotActivoEjecucion].duracionMinutos)
      restante=slotsMemoria[slotActivoEjecucion].duracionMinutos-transcurrido;
    tft.setTextColor(ILI9341_YELLOW); tft.setCursor(180,100);
    tft.print("Restan:"); tft.print(restante); tft.print("m");
    if(cicloPausado){tft.setTextColor(0xFD20); tft.print(" PAUSA");}
  }
  tft.fillRect( 15,110, 65,35,t.activoOFF); tft.drawRect( 15,110, 65,35,t.borde);
  tft.setTextColor(t.textoPrincipal); tft.setCursor(25,122); tft.print("- Temp");
  tft.fillRect( 90,110, 65,35,t.activoON);  tft.drawRect( 90,110, 65,35,t.borde);
  tft.setCursor(100,122); tft.print("+ Temp");
  tft.fillRect(165,110,140,35,t.panel);      tft.drawRect(165,110,140,35,t.borde);
  tft.setCursor(185,122); tft.print("CONFIG SLOT");
  bool enCiclo=cicloIniciado&&!cicloPausado;
  uint16_t colCiclo=cicloPausado?0xFD20:(enCiclo?t.activoOFF:t.activoON);
  tft.fillRect(15,155,290,38,colCiclo); tft.drawRect(15,155,290,38,t.borde);
  tft.setTextColor(t.textoPrincipal); tft.setCursor(50,168);
  if      (!cicloIniciado)tft.print("INICIAR CICLO AUTOMATICO");
  else if (cicloPausado)  tft.print("CICLO PAUSADO  HORN=reanudar");
  else                    tft.print("DETENER CICLO");
}

void dibujarPantallaQR() {
  tft.setTextColor(t.textoAcento); tft.setTextSize(1);
  tft.setCursor(15,30); tft.print("VINCULACION BLUETOOTH");
  tft.fillRect(40,45,240,120,t.panel); tft.drawRect(40,45,240,120,t.borde);
  tft.setTextColor(t.textoAcento); tft.setTextSize(2);
  tft.setCursor(70,58); tft.print("BLE DEVICE");
  tft.setTextSize(1); tft.setTextColor(t.textoPrincipal);
  tft.setCursor(55, 88); tft.print("Nombre: TermostatoGame");
  tft.setCursor(55,105); tft.print("UP/DOWN=cursor  A=OK");
  tft.setCursor(55,120); tft.print("LIGHT=Emergencia");
  tft.setCursor(55,135); tft.print("HORN=Pausa/Apaga animacion/Emergencia");
  tft.fillRect(40,175,240,25,dispositivoConectado?t.activoON:t.activoOFF);
  tft.drawRect(40,175,240,25,t.borde);
  tft.setTextColor(t.textoPrincipal); tft.setCursor(90,182);
  tft.print(dispositivoConectado?"[ CONECTADO ]":"[ ESPERANDO... ]");
}

// modos
void ejecutarLogicaModos() {
  if (modoOperacion==1){
    float h=0.5;
    if      (temperaturaActual>(temperaturaObjetivo+h))
      {estadoCompresor=true; estadoVentilador=true; estadoSuministro=true;}
    else if (temperaturaActual<=temperaturaObjetivo)
      {estadoCompresor=false; estadoVentilador=false; estadoSuministro=false;}
    aplicarReles(); return;
  }
  if (modoOperacion==2&&cicloIniciado&&!cicloPausado){
    SlotCiclo &s=slotsMemoria[slotActivoEjecucion]; float h=0.5;
    if      (temperaturaActual>(s.tempMeta+h))
      {estadoCompresor=s.relCompresor; estadoVentilador=s.relVentilador;
       estadoSuministro=s.relSuministro; estadoRecirc=s.relRecirc;}
    else if (temperaturaActual<=s.tempMeta)
      {estadoCompresor=false; estadoVentilador=false; estadoSuministro=false; estadoRecirc=false;}
    aplicarReles();
    unsigned long mins=(millis()-tiempoInicioSlot+tiempoAcumuladoSlot)/60000;
    if (mins>=s.duracionMinutos){
      if (slotActivoEjecucion<4){
        slotActivoEjecucion++; tiempoInicioSlot=millis(); tiempoAcumuladoSlot=0;
        Serial.print("Slot -> "); Serial.println(slotActivoEjecucion+1);
      } else {
        cicloIniciado=false; apagarTodo(); iniciarAnimacion(2);
      }
    }
  }
}

void aplicarReles() {
  digitalWrite(RELAY_COMPRESOR, estadoCompresor ?HIGH:LOW);
  digitalWrite(RELAY_VENTILADOR,estadoVentilador?HIGH:LOW);
  digitalWrite(RELAY_SUMINISTRO,estadoSuministro?HIGH:LOW);
  digitalWrite(RELAY_RECIRC,    estadoRecirc    ?HIGH:LOW);
}

void apagarTodo() {
  estadoSuministro=false; estadoRecirc=false;
  estadoVentilador=false; estadoCompresor=false;
  modoOperacion=0; cicloIniciado=false; cicloPausado=false;
  campoSeleccionado=0; enEmergencia=false;
  digitalWrite(RELAY_SUMINISTRO,LOW); digitalWrite(RELAY_RECIRC,    LOW);
  digitalWrite(RELAY_VENTILADOR,LOW); digitalWrite(RELAY_COMPRESOR, LOW);
}