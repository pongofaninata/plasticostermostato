#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

#define SERVICE_UUID           "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHARACTERISTIC_UUID_RX "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHARACTERISTIC_UUID_TX "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"

BLECharacteristic *pCharacteristic;
bool dispositivoConectado = false;

#define TAM_COLA 8
String colaCmd[TAM_COLA];
volatile uint8_t colaIni = 0, colaFin = 0;
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

#define RELE_ACTIVO_BAJO false
void escribirRele(int pin, bool on) {
  digitalWrite(pin, (on != RELE_ACTIVO_BAJO) ? HIGH : LOW);
}

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

// temas
struct ColoresTema {
  uint16_t fondo, panel, textoPrincipal, textoAcento, borde, activoON, activoOFF;
};
ColoresTema t;

void actualizarPaletaColores() {
  if (temaEstetico == 0)      t = { 0x0841, 0x28A1, 0xCE59, 0xC800, 0x8000, 0xD8E0, 0x5142 };
  else if (temaEstetico == 1) t = { 0x0040, 0x00E0, 0x564A, 0x07E0, 0x03C0, 0x0720, 0x01E0 };
  else if (temaEstetico == 2) t = { 0xF75B, 0xB69C, 0x10E7, 0xDD43, 0x5416, 0xCCA0, 0x8D15 };
  else                        t = { 0x0001, 0x1805, 0xDEFF, 0x06FF, 0xC816, 0xFBC0, 0x380A };
}

void dibujarDetalleTemaTitulo(int y) {
  if (temaEstetico == 0) {
    for (int i = 0; i < 320; i += 16) tft.fillRect(i, y, 8, 3, 0xC800);
  }
  else if (temaEstetico == 1) {
    for (int i = 0; i < 320; i += 4) {
      tft.drawPixel(i, y,   0x07E0);
      tft.drawPixel(i+1, y, 0x03C0);
    }
  }
  else if (temaEstetico == 2) {
    tft.drawFastHLine(0, y, 320, 0xDD43);
    for (int i = 10; i < 310; i += 40) tft.fillTriangle(i, y-4, i+4, y, i+8, y-4, 0xDD43);
  }
  else {
    for (int i = 0; i < 320; i += 6) {
      uint16_t c = (i % 12 == 0) ? 0xC816 : 0x06FF;
      tft.drawFastVLine(i, y-1, 3, c);
    }
  }
}

void dibujarMarcoTematico(int x, int y, int w, int h) {
  tft.drawRect(x, y, w, h, t.borde);
  if (temaEstetico == 0) {
    tft.fillRect(x,       y,     5, 5, 0xC800);
    tft.fillRect(x+w-5,   y,     5, 5, 0xC800);
    tft.fillRect(x,       y+h-5, 5, 5, 0xC800);
    tft.fillRect(x+w-5,   y+h-5, 5, 5, 0xC800);
  }
  else if (temaEstetico == 1) {
    tft.drawRect(x-1, y-1, w+2, h+2, 0x0180);
  }
  else if (temaEstetico == 2) {
    tft.drawLine(x,   y+2, x+2, y,   0xDD43);
    tft.drawLine(x+w-3, y, x+w-1, y+2, 0xDD43);
    tft.drawLine(x,   y+h-3, x+2, y+h-1, 0xDD43);
    tft.drawLine(x+w-3, y+h-1, x+w-1, y+h-3, 0xDD43);
  }
  else {
    tft.drawRect(x+1, y+1, w-2, h-2, 0x0480);
  }
}

const char* nombreTema() {
  if (temaEstetico == 0) return "DOOM";
  if (temaEstetico == 1) return "FALLOUT";
  if (temaEstetico == 2) return "ZELDA";
  return "METROID";
}

float temperaturaActual   = 24.5;
float ultimaTempMostrada  = -999;
float temperaturaObjetivo = 18.0;

int horaActual    = 12, minutoActual = 0, segundoActual = 0;
int horaAlarma    =  7, minutoAlarma = 30;
bool alarmaActiva    = true;
bool alarmaDisparada = false;
//
int campoSeleccionado = 0;

unsigned long tiempoUltimoSegundo = 0;
unsigned long ultimaLecturaTemp   = 0;
unsigned long tiempoSolicitudTemp = 0;
bool          tempSolicitada      = false;
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
// minutos 
unsigned long minutosTranscurridos() {
  unsigned long ms = cicloPausado ? tiempoAcumuladoSlot : (millis() - tiempoInicioSlot);
  return ms / 60000UL;
}
struct Boton { int x, y, w, h; const char* etiqueta; };
Boton botonesDashboard[] = {
  { 215, 26,  90, 20, "TEMA"       },
  { 125, 95,  45, 20, "SUMINISTRO" },
  { 235, 95,  45, 20, "RECIRC"     },
  { 125,128,  45, 20, "VENTILADOR" },
  { 235,128,  45, 20, "COMPRESOR"  },
  {   0,205,  60, 35, "<"          },
  { 260,205,  60, 35, ">"          }
};
int totalBotonesDashboard = 7;

Boton botonesTermostato[] = {
  {  15,120, 135, 40, "-0.5C" },
  { 170,120, 135, 40, "+0.5C" },
  {  15,170, 290, 28, "MODO"  },
  {   0,205,  60, 35, "<"     },
  { 260,205,  60, 35, ">"     }
};
int totalBotonesTermostato = 5;

// 0=H.SIS 1=M.SIS 2=AMPM.SIS 3=H.ALM 4=M.ALM 5=AMPM.ALM 6=ALM.ON 7=< 8=>
Boton botonesReloj[] = {
  {  15, 58,  55, 28, "H.SIS"    },
  {  78, 58,  55, 28, "M.SIS"    },
  { 168, 58,  52, 28, "AMPM.SIS" },
  {  15,120,  55, 28, "H.ALM"    },
  {  78,120,  55, 28, "M.ALM"    },
  { 142,120,  48, 28, "AMPM.ALM" },
  { 194,120, 116, 28, "ALM.ON"   },
  {   0,205,  60, 35, "<"        },
  { 260,205,  60, 35, ">"        }
};
int totalBotonesReloj = 9;

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

//
void apagarTodo();
void apagarReles();
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

int hora12(int h24) { int h = h24 % 12; return (h == 0) ? 12 : h; }
bool esAm(int h24)  { return h24 < 12; }

String formatoAmPm(int h24, int m, int s, bool conSegundos) {
  char buf[16];
  if (conSegundos)
    sprintf(buf, "%02d:%02d:%02d %s", hora12(h24), m, s, esAm(h24) ? "AM" : "PM");
  else
    sprintf(buf, "%02d:%02d %s", hora12(h24), m, esAm(h24) ? "AM" : "PM");
  return String(buf);
}

// 
class MyServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer* s)    { dispositivoConectado = true; }
  void onDisconnect(BLEServer* s) { dispositivoConectado = false; s->startAdvertising(); }
};

class MyCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *p) {
    String cmd = String(p->getValue().c_str());
    cmd.trim();
    if (cmd.length() == 0) return;
    Serial.print("BLE:["); Serial.print(cmd); Serial.println("]");
    uint8_t sig = (colaFin + 1) % TAM_COLA;
    if (sig != colaIni) { colaCmd[colaFin] = cmd; colaFin = sig; }
  }
};
//
void dibujarBarraSuperior() {
  uint16_t colorBarra;
  if      (temaEstetico == 0) colorBarra = 0x2000;
  else if (temaEstetico == 1) colorBarra = 0x00A0;
  else if (temaEstetico == 2) colorBarra = 0x4A9F;
  else                        colorBarra = 0x1004;

  tft.fillRect(0, 0, 320, 24, colorBarra);

  tft.setTextColor(t.textoAcento); tft.setTextSize(1);
  tft.setCursor(4, 8);
  tft.print(formatoAmPm(horaActual, minutoActual, segundoActual, true));

  if (alarmaActiva) {
    tft.setTextColor(alarmaDisparada ? ILI9341_YELLOW : 0x8410);
    tft.setCursor(110, 8); tft.print("ALM ");
    tft.print(formatoAmPm(horaAlarma, minutoAlarma, 0, false));
  }
  tft.setCursor(220, 8);
  if      (modoOperacion == 0) { tft.setTextColor(0x8410);        tft.print("MAN"); }
  else if (modoOperacion == 1) { tft.setTextColor(ILI9341_GREEN); tft.print("TER"); }
  else                         { tft.setTextColor(0xFFE0);        tft.print("CIC"); }

  tft.setTextColor(t.textoPrincipal); tft.setCursor(250, 8);
  tft.print(temperaturaActual, 1); tft.print("C");

  tft.setCursor(300, 8);
  if (dispositivoConectado) { tft.setTextColor(0x001F); tft.print("B"); }
  else                      { tft.setTextColor(0x8410); tft.print("-"); }

  dibujarDetalleTemaTitulo(24);
}
// 
void iniciarAnimacion(int tipo) {
  animacionActiva = tipo; animFrameCounter = 0; animToggle = false;
  apagarReles();               // solo relés
  tft.fillScreen(ILI9341_BLACK);
}

void tickAnimacion() {
  if (animacionActiva == 0) return;
  animFrameCounter++; animToggle = !animToggle;

  if (animacionActiva == 1) {   // solo alarma
    uint16_t col1 = animToggle ? ILI9341_RED    : ILI9341_BLACK;
    uint16_t col2 = animToggle ? ILI9341_YELLOW : ILI9341_RED;
    tft.fillScreen(col1); dibujarBarraSuperior();
    tft.setTextColor(col2); tft.setTextSize(3);
    tft.setCursor(30, 50);  tft.print("! ALARMA !");
    tft.setCursor(30, 90);  tft.print("! ALARMA !");
    tft.setTextSize(2); tft.setTextColor(ILI9341_WHITE);
    tft.setCursor(70, 135);
    tft.print(formatoAmPm(horaAlarma, minutoAlarma, 0, false));
    tft.setTextSize(1); tft.setTextColor(ILI9341_CYAN);
    tft.setCursor(35, 178); tft.print("Presiona  HORN  para apagar");
    escribirRele(RELAY_VENTILADOR, animToggle);
    escribirRele(RELAY_RECIRC,     !animToggle);
    if (animFrameCounter >= 30) detenerAnimacion();
    return;
  }

  if (animacionActiva == 2) {   // finciclo
    uint16_t cols[3] = { 0xF81F, 0x001F, 0x07E0 };
    uint16_t colFondo = cols[animFrameCounter % 3];
    uint16_t colTexto = animToggle ? ILI9341_WHITE : ILI9341_YELLOW;
    tft.fillScreen(colFondo); dibujarBarraSuperior();
    tft.setTextSize(2); tft.setTextColor(colTexto);
    tft.setCursor(18, 50);  tft.print("CICLO COMPLETO");
    tft.setCursor(18, 78);  tft.print("==============");
    tft.setTextSize(1); tft.setTextColor(ILI9341_WHITE);
    tft.setCursor(30, 112); tft.print("Ultimo slot: ");
    tft.print(slotsMemoria[slotActivoEjecucion].nombreSlot);
    for (int i = 0; i < 5; i++) {
      uint16_t c = animToggle ? ILI9341_YELLOW : ILI9341_WHITE;
      tft.fillRect(20 + i * 58, 138, 48, 10 + (i % 3) * 8, c);
    }
    tft.setTextSize(1); tft.setTextColor(ILI9341_WHITE);
    tft.setCursor(30, 180); tft.print("Presiona  HORN  para continuar");
    if (animFrameCounter <= 6 && animToggle) {
      escribirRele(RELAY_COMPRESOR, true); escribirRele(RELAY_VENTILADOR, true);
      delay(150);
      escribirRele(RELAY_COMPRESOR, false); escribirRele(RELAY_VENTILADOR, false);
    }
    if (animFrameCounter >= 24) detenerAnimacion();
  }
}

void detenerAnimacion() {
  animacionActiva = 0; animFrameCounter = 0; alarmaDisparada = false;
  apagarReles();
  refrescarPantallaActual();
}
// 
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
      {  15, 55, 290, 25, "TEMP"       },
      {  15, 85, 290, 25, "DURACION"   },
      {  15,115, 290, 25, "COMPRESOR"  },
      {  15,145, 290, 25, "VENTILADOR" },
      {  15,175, 290, 25, "SUMINISTRO" },
      {  15,205, 145, 25, "RECIRC"     },
      { 165,205, 140, 25, "GUARDAR"    }
    };
    return sub[cursorSubmenu];
  }
  if (pantallaActual == 0) return botonesDashboard[cursorPos];
  if (pantallaActual == 1) return botonesTermostato[cursorPos];
  if (pantallaActual == 2) return botonesReloj[cursorPos];
  if (pantallaActual == 3) return botonesSlots[cursorPos];
  return botonesBLE[cursorPos];
}
void dibujarCursor() {
  Boton b = botonActual();
  uint16_t colorCursor = t.textoAcento;
  tft.drawRect(b.x - 2, b.y - 2, b.w + 4, b.h + 4, colorCursor);
  tft.drawRect(b.x - 3, b.y - 3, b.w + 6, b.h + 6, colorCursor);
}
void borrarCursor() {
  Boton b = botonActual();
  tft.drawRect(b.x - 2, b.y - 2, b.w + 4, b.h + 4, t.fondo);
  tft.drawRect(b.x - 3, b.y - 3, b.w + 6, b.h + 6, t.fondo);
}
void flashVerde() {
  Boton b = botonActual();
  tft.drawRect(b.x, b.y, b.w, b.h, ILI9341_GREEN); delay(80);
  tft.drawRect(b.x, b.y, b.w, b.h, t.borde);
}
// submenu slot
void dibujarSubmenuSlot() {
  actualizarPaletaColores();
  tft.fillScreen(t.fondo);
  dibujarBarraSuperior();

  SlotCiclo &s = slotsMemoria[slotSeleccionadoConfig];
  tft.setTextColor(t.textoAcento); tft.setTextSize(1);
  tft.setCursor(10, 28); tft.print("CONFIG SLOT ");
  tft.print(slotSeleccionadoConfig + 1); tft.print(": "); tft.print(s.nombreSlot);
char v0[12], v1[12];
  sprintf(v0, "%.1f C", s.tempMeta);
  sprintf(v1, "%lu min", s.duracionMinutos);
struct F { int y; const char* lbl; const char* val; const char* hint; uint16_t vc; };
  F filas[5] = {
    {  55, "Temp meta:  ", v0,  "[L/R]", t.textoAcento },
    {  85, "Duracion:   ", v1,  "[L/R]", t.textoAcento },
    { 115, "Compresor:  ", s.relCompresor  ? "ON " : "OFF", "[A]", s.relCompresor  ? t.activoON : t.activoOFF },
    { 145, "Ventilador: ", s.relVentilador ? "ON " : "OFF", "[A]", s.relVentilador ? t.activoON : t.activoOFF },
    { 175, "Suministro: ", s.relSuministro ? "ON " : "OFF", "[A]", s.relSuministro ? t.activoON : t.activoOFF }
  };
  for (int i = 0; i < 5; i++) {
    tft.fillRect(15, filas[i].y, 290, 25, t.panel);
    dibujarMarcoTematico(15, filas[i].y, 290, 25);
    tft.setTextColor(t.textoPrincipal); tft.setCursor(20, filas[i].y + 8); tft.print(filas[i].lbl);
    tft.setTextColor(filas[i].vc);      tft.print(filas[i].val);
    tft.setTextColor(0x8410);           tft.setCursor(245, filas[i].y + 8); tft.print(filas[i].hint);
  }
  tft.fillRect(15, 205, 145, 25, t.panel);
  dibujarMarcoTematico(15, 205, 145, 25);
  tft.setTextColor(t.textoPrincipal); tft.setCursor(20, 213); tft.print("Recirc: ");
  tft.setTextColor(s.relRecirc ? t.activoON : t.activoOFF); tft.print(s.relRecirc ? "ON" : "OFF");

  tft.fillRect(165, 205, 140, 25, t.activoON);
  dibujarMarcoTematico(165, 205, 140, 25);
  tft.setTextColor(ILI9341_WHITE); tft.setCursor(195, 213); tft.print("GUARDAR [A]");
  dibujarCursor();
}
// a
void confirmarBoton() {
  flashVerde();

  if (enSubmenuSlot) {
    SlotCiclo &s = slotsMemoria[slotSeleccionadoConfig];
    switch (cursorSubmenu) {
      case 2: s.relCompresor  = !s.relCompresor;  dibujarSubmenuSlot(); return;
      case 3: s.relVentilador = !s.relVentilador; dibujarSubmenuSlot(); return;
      case 4: s.relSuministro = !s.relSuministro; dibujarSubmenuSlot(); return;
      case 5: s.relRecirc     = !s.relRecirc;     dibujarSubmenuSlot(); return;
      case 6: enSubmenuSlot = false; cursorPos = 0; cursorSubmenu = 0; refrescarPantallaActual(); return;
      default: dibujarCursor(); return;
    }
  }

  if (pantallaActual == 0) {
    if (cursorPos == 0) { temaEstetico = (temaEstetico + 1) % 4; refrescarPantallaActual(); return; }
    if (modoOperacion == 0) {
      if (cursorPos == 1) { estadoSuministro = !estadoSuministro; aplicarReles(); dibujarDashboard(); dibujarCursor(); return; }
      if (cursorPos == 2) { estadoRecirc     = !estadoRecirc;     aplicarReles(); dibujarDashboard(); dibujarCursor(); return; }
      if (cursorPos == 3) { estadoVentilador = !estadoVentilador; aplicarReles(); dibujarDashboard(); dibujarCursor(); return; }
      if (cursorPos == 4) { estadoCompresor  = !estadoCompresor;  aplicarReles(); dibujarDashboard(); dibujarCursor(); return; }
    }
    if (cursorPos == 5) { pantallaActual = (pantallaActual == 0) ? 4 : pantallaActual - 1; cursorPos = 0; refrescarPantallaActual(); return; }
    if (cursorPos == 6) { pantallaActual = (pantallaActual + 1) % 5; cursorPos = 0; refrescarPantallaActual(); return; }
  }
  else if (pantallaActual == 1) {
    if (cursorPos == 0 && temperaturaObjetivo > 2.0)  { temperaturaObjetivo -= 0.5; dibujarControlTemperatura(); dibujarCursor(); return; }
    if (cursorPos == 1 && temperaturaObjetivo < 40.0) { temperaturaObjetivo += 0.5; dibujarControlTemperatura(); dibujarCursor(); return; }
    if (cursorPos == 2) { modoOperacion = (modoOperacion == 1) ? 0 : 1; dibujarControlTemperatura(); dibujarCursor(); return; }
    if (cursorPos == 3) { pantallaActual--; if (pantallaActual < 0) pantallaActual = 4; cursorPos = 0; refrescarPantallaActual(); return; }
    if (cursorPos == 4) { pantallaActual = (pantallaActual + 1) % 5; cursorPos = 0; refrescarPantallaActual(); return; }
  }
  else if (pantallaActual == 2) {
    if (cursorPos == 0) { campoSeleccionado = (campoSeleccionado == 1) ? 0 : 1; dibujarRelojAlarma(); dibujarCursor(); return; }
    if (cursorPos == 1) { campoSeleccionado = (campoSeleccionado == 2) ? 0 : 2; dibujarRelojAlarma(); dibujarCursor(); return; }
    if (cursorPos == 2) {
      if (horaActual < 12) horaActual += 12; else horaActual -= 12;
      dibujarRelojAlarma(); dibujarCursor(); return;
    }
    if (cursorPos == 3) { campoSeleccionado = (campoSeleccionado == 3) ? 0 : 3; dibujarRelojAlarma(); dibujarCursor(); return; }
    if (cursorPos == 4) { campoSeleccionado = (campoSeleccionado == 4) ? 0 : 4; dibujarRelojAlarma(); dibujarCursor(); return; }
    if (cursorPos == 5) {
      if (horaAlarma < 12) horaAlarma += 12; else horaAlarma -= 12;
      dibujarRelojAlarma(); dibujarCursor(); return;
    }
    if (cursorPos == 6) {
      alarmaActiva = !alarmaActiva; dibujarRelojAlarma(); dibujarCursor(); return;
    }
    if (cursorPos == 7) {
      pantallaActual--; if (pantallaActual < 0) pantallaActual = 4;
      cursorPos = 0; campoSeleccionado = 0; refrescarPantallaActual(); return;
    }
    if (cursorPos == 8) {
      pantallaActual = (pantallaActual + 1) % 5;
      cursorPos = 0; campoSeleccionado = 0; refrescarPantallaActual(); return;
    }
  }
  else if (pantallaActual == 3) {
    if (cursorPos == 0 && slotsMemoria[slotSeleccionadoConfig].tempMeta > 2.0)  { slotsMemoria[slotSeleccionadoConfig].tempMeta -= 0.5; dibujarInterfazSlots(); dibujarCursor(); return; }
    if (cursorPos == 1 && slotsMemoria[slotSeleccionadoConfig].tempMeta < 40.0) { slotsMemoria[slotSeleccionadoConfig].tempMeta += 0.5; dibujarInterfazSlots(); dibujarCursor(); return; }
    if (cursorPos == 2) { enSubmenuSlot = true; cursorSubmenu = 0; dibujarSubmenuSlot(); return; }
    if (cursorPos == 3) {
      if (!cicloIniciado) {
        cicloIniciado = true; cicloPausado = false;
        slotActivoEjecucion = slotSeleccionadoConfig;
        tiempoInicioSlot = millis(); tiempoAcumuladoSlot = 0; modoOperacion = 2;
      } else { apagarTodo(); }
      dibujarInterfazSlots(); dibujarCursor(); return;
    }
    if (cursorPos == 4) { pantallaActual--; if (pantallaActual < 0) pantallaActual = 4; cursorPos = 0; refrescarPantallaActual(); return; }
    if (cursorPos == 5) { pantallaActual = (pantallaActual + 1) % 5; cursorPos = 0; refrescarPantallaActual(); return; }
  }
  else if (pantallaActual == 4) {
    if (cursorPos == 0) { pantallaActual = 3; cursorPos = 0; refrescarPantallaActual(); return; }
    if (cursorPos == 1) { pantallaActual = 0; cursorPos = 0; refrescarPantallaActual(); return; }
  }
}
// up/down
bool procesarUpDownCampo(bool subir) {
  if (campoSeleccionado == 0) return false;
  if (campoSeleccionado == 1) {
    horaActual = subir ? (horaActual + 1) % 24 : (horaActual == 0 ? 23 : horaActual - 1);
    segundoActual = 0; dibujarRelojAlarma(); dibujarCursor(); return true;
  }
  if (campoSeleccionado == 2) {
    minutoActual = subir ? (minutoActual + 1) % 60 : (minutoActual == 0 ? 59 : minutoActual - 1);
    segundoActual = 0; dibujarRelojAlarma(); dibujarCursor(); return true;
  }
  if (campoSeleccionado == 3) {
    horaAlarma = subir ? (horaAlarma + 1) % 24 : (horaAlarma == 0 ? 23 : horaAlarma - 1);
    dibujarRelojAlarma(); dibujarCursor(); return true;
  }
  if (campoSeleccionado == 4) {
    minutoAlarma = subir ? (minutoAlarma + 1) % 60 : (minutoAlarma == 0 ? 59 : minutoAlarma - 1);
    dibujarRelojAlarma(); dibujarCursor(); return true;
  }
  return false;
}

//ble
void procesarComandoBLE(String cmd) {
// horn
  if (cmd == "HORN") {
    if (enEmergencia) { enEmergencia = false; refrescarPantallaActual(); return; }
    if (animacionActiva) { detenerAnimacion(); return; }
    if (cicloIniciado && !cicloPausado) {
      tiempoAcumuladoSlot = millis() - tiempoInicioSlot;
      cicloPausado = true; apagarReles(); modoOperacion = 0;
      Serial.println("Ciclo pausado.");
      refrescarPantallaActual(); return;
    }
    if (cicloIniciado && cicloPausado) {
      tiempoInicioSlot = millis() - tiempoAcumuladoSlot;
      cicloPausado = false; modoOperacion = 2;
      Serial.println("Ciclo reanudado.");
      refrescarPantallaActual(); return;
    }
    return;
  }
// paroemergencia
  if (cmd == "LIGHT") {
    apagarTodo(); enSubmenuSlot = false; animacionActiva = 0; enEmergencia = true;
    tft.fillScreen(ILI9341_RED);
    dibujarBarraSuperior();
    tft.setTextColor(ILI9341_WHITE); tft.setTextSize(3);
    tft.setCursor(10, 60);  tft.print("EMERGENCIA");
    tft.setCursor(10, 100); tft.print("DETENIDO");
    tft.setTextSize(1); tft.setCursor(30, 155);
    tft.print("Todo apagado. HORN para volver.");
    Serial.println("!!! PARO DE EMERGENCIA !!!");
    return;
  }

  if (cmd == "D") return;
  if (enEmergencia || animacionActiva) return;

  //up
  if (cmd == "UP") {
    if (enSubmenuSlot) { borrarCursor(); cursorSubmenu = (cursorSubmenu == 0) ? 6 : cursorSubmenu - 1; dibujarCursor(); return; }
    if (pantallaActual == 2 && procesarUpDownCampo(true)) return;
    borrarCursor(); cursorPos = (cursorPos == 0) ? totalBotonesPantallaActual() - 1 : cursorPos - 1; dibujarCursor(); return;
  }

  //down
  if (cmd == "DOWN") {
    if (enSubmenuSlot) { borrarCursor(); cursorSubmenu = (cursorSubmenu + 1) % 7; dibujarCursor(); return; }
    if (pantallaActual == 2 && procesarUpDownCampo(false)) return;
    borrarCursor(); cursorPos = (cursorPos + 1) % totalBotonesPantallaActual(); dibujarCursor(); return;
  }

  //left
  if (cmd == "LEFT") {
    if (enSubmenuSlot) {
      SlotCiclo &s = slotsMemoria[slotSeleccionadoConfig];
      if (cursorSubmenu == 0 && s.tempMeta > 2.0)      { s.tempMeta -= 0.5;    dibujarSubmenuSlot(); return; }
      if (cursorSubmenu == 1 && s.duracionMinutos > 5)  { s.duracionMinutos -= 5; dibujarSubmenuSlot(); return; }
      return;
    }
    if (pantallaActual == 3) { slotSeleccionadoConfig = (slotSeleccionadoConfig == 0) ? 4 : slotSeleccionadoConfig - 1; dibujarInterfazSlots(); dibujarCursor(); return; }
    borrarCursor(); cursorPos = (cursorPos == 0) ? totalBotonesPantallaActual() - 1 : cursorPos - 1; dibujarCursor(); return;
  }

  //right
  if (cmd == "RIGHT") {
    if (enSubmenuSlot) {
      SlotCiclo &s = slotsMemoria[slotSeleccionadoConfig];
      if (cursorSubmenu == 0 && s.tempMeta < 40.0)       { s.tempMeta += 0.5;    dibujarSubmenuSlot(); return; }
      if (cursorSubmenu == 1 && s.duracionMinutos < 480)  { s.duracionMinutos += 5; dibujarSubmenuSlot(); return; }
      return;
    }
    if (pantallaActual == 3) { slotSeleccionadoConfig = (slotSeleccionadoConfig + 1) % 5; dibujarInterfazSlots(); dibujarCursor(); return; }
    borrarCursor(); cursorPos = (cursorPos + 1) % totalBotonesPantallaActual(); dibujarCursor(); return;
  }

  //a
  if (cmd == "A") { confirmarBoton(); return; }

  //b
  if (cmd == "B") {
    if (enSubmenuSlot) { enSubmenuSlot = false; cursorSubmenu = 0; cursorPos = 0; refrescarPantallaActual(); return; }
    borrarCursor(); cursorPos = 0; campoSeleccionado = 0;
    pantallaActual = (pantallaActual == 0) ? 4 : pantallaActual - 1;
    refrescarPantallaActual(); return;
  }

  //c
  if (cmd == "C") {
    if (enSubmenuSlot) { enSubmenuSlot = false; cursorSubmenu = 0; cursorPos = 0; refrescarPantallaActual(); return; }
    borrarCursor(); cursorPos = 0; campoSeleccionado = 0;
    pantallaActual = (pantallaActual + 1) % 5;
    refrescarPantallaActual(); return;
  }
}

//setup
void setup() {
  Serial.begin(115200);
  pinMode(RELAY_SUMINISTRO, OUTPUT); pinMode(RELAY_RECIRC,    OUTPUT);
  pinMode(RELAY_VENTILADOR, OUTPUT); pinMode(RELAY_COMPRESOR, OUTPUT);
  apagarTodo();

  SPI.begin(SPI_SCK, SPI_MISO, SPI_MOSI);
  tft.begin(); tft.setRotation(3);

  sensors.begin();
  sensors.setWaitForConversion(false); 

  BLEDevice::init("TermostatoGame");
  BLEDevice::setMTU(185);
  BLEServer *pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());
  BLEService *pService = pServer->createService(SERVICE_UUID);
  pCharacteristic = pService->createCharacteristic(CHARACTERISTIC_UUID_TX, BLECharacteristic::PROPERTY_NOTIFY);
  pCharacteristic->addDescriptor(new BLE2902());
  BLECharacteristic *pRx = pService->createCharacteristic(
      CHARACTERISTIC_UUID_RX,
      BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);
  pRx->setCallbacks(new MyCallbacks());
  pService->start(); pServer->startAdvertising();

  actualizarPaletaColores();
  tft.fillScreen(t.fondo);
  refrescarPantallaActual();
  tiempoUltimoSegundo = millis();
  Serial.println("Sistema listo.");
}

// loop
void loop() {
  while (colaIni != colaFin) {
    String c = colaCmd[colaIni];
    colaIni = (colaIni + 1) % TAM_COLA;
    procesarComandoBLE(c);
  }
  if (millis() - tiempoUltimoSegundo >= 1000) {
    tiempoUltimoSegundo += 1000;
    if (campoSeleccionado == 0 && !enEmergencia) {
      segundoActual++;
      if (segundoActual >= 60) {
        segundoActual = 0; minutoActual++;
        if (minutoActual >= 60) { minutoActual = 0; horaActual = (horaActual + 1) % 24; }
      }
    }
    if (!animacionActiva && !enEmergencia) dibujarBarraSuperior();

    if (alarmaActiva && !alarmaDisparada &&
        horaActual == horaAlarma && minutoActual == minutoAlarma && segundoActual == 0) {
      alarmaDisparada = true;
      iniciarAnimacion(1);
    }
  }
  if (animacionActiva && millis() - ultimaAnimFrame >= 250) {
    ultimaAnimFrame = millis(); tickAnimacion();
  }

  // 4) Temperatura (no bloqueante: pedir, esperar ~800 ms, leer)
  if (!animacionActiva && !enEmergencia) {
    if (!tempSolicitada && millis() - ultimaLecturaTemp > 2000) {
      sensors.requestTemperatures();
      tempSolicitada = true;
      tiempoSolicitudTemp = millis();
    }
    else if (tempSolicitada && millis() - tiempoSolicitudTemp >= 800) {
      tempSolicitada = false;
      ultimaLecturaTemp = millis();
      float tmp = sensors.getTempCByIndex(0);
      if (tmp != -127.0 && tmp != 85.0) temperaturaActual = tmp;
      actualizarValoresDinamicos();
      ejecutarLogicaModos();

    
        if (dispositivoConectado) {
  char payload[110];
  snprintf(payload, sizeof(payload),
    "Temp:%.1f|Meta:%.1f|Modo:%d|Slot:%d|Ciclo:%d|Sumi:%d|Recirc:%d|Vent:%d|Comp:%d|Pan:%d|Pau:%d\n",
    temperaturaActual, temperaturaObjetivo, modoOperacion,
    slotActivoEjecucion, cicloIniciado ? 1 : 0,
    estadoSuministro, estadoRecirc, estadoVentilador, estadoCompresor,
    pantallaActual, cicloPausado ? 1 : 0);
  pCharacteristic->setValue((uint8_t*)payload, strlen(payload));
  pCharacteristic->notify();
}
      }
    }
  }

// refresh
void refrescarPantallaActual() {
  if (enSubmenuSlot) { dibujarSubmenuSlot(); return; }
  actualizarPaletaColores();
  tft.fillScreen(t.fondo);
  dibujarBarraSuperior();

  tft.fillRect(  0, 205, 60, 35, t.panel);
  dibujarMarcoTematico(0, 205, 60, 35);
  tft.setTextColor(t.textoPrincipal); tft.setTextSize(2); tft.setCursor(20, 213); tft.print("<");

  tft.fillRect(260, 205, 60, 35, t.panel);
  dibujarMarcoTematico(260, 205, 60, 35);
  tft.setCursor(280, 213); tft.print(">");

  tft.fillRect(61, 205, 198, 35, t.fondo);
  tft.setTextSize(1); tft.setTextColor(t.textoAcento); tft.setCursor(72, 216);
  const char* nombres[] = { "[DASHBOARD]", "[TERMOSTATO]", "[RELOJ & ALARMA]", "[SLOTS]", "[BLUETOOTH]" };
  tft.print(nombres[pantallaActual]);

  tft.setTextColor(0x8410); tft.setCursor(200, 226); tft.print(nombreTema());

  switch (pantallaActual) {
    case 0: dibujarDashboard();          break;
    case 1: dibujarControlTemperatura(); break;
    case 2: dibujarRelojAlarma();        break;
    case 3: dibujarInterfazSlots();      break;
    case 4: dibujarPantallaQR();         break;
  }
  ultimaTempMostrada = -999;
  dibujarCursor();
}
//pantalla
void dibujarDashboard() {
  tft.setTextColor(t.textoAcento); tft.setTextSize(1);
  tft.setCursor(15, 28);
  if      (temaEstetico == 0) tft.print("// DOOM HUD //");
  else if (temaEstetico == 1) tft.print("* PIP-BOY 3000 *");
  else if (temaEstetico == 2) tft.print("~ TABLERO HYRULE ~");
  else                        tft.print("[ VISOR SAMUS ]");

  tft.fillRect(215, 26, 90, 20, t.panel);
  dibujarMarcoTematico(215, 26, 90, 20);
  tft.setTextColor(t.textoPrincipal); tft.setCursor(222, 32);
  tft.print("TEMA:"); tft.print(nombreTema());

  tft.fillRect(15, 50, 290, 26, t.panel);
  dibujarMarcoTematico(15, 50, 290, 26);
  tft.setTextColor(t.textoPrincipal); tft.setTextSize(1);
  tft.setCursor(25, 58); tft.print("Sensor:");
  tft.setTextSize(2); tft.setTextColor(t.textoAcento);
  tft.setCursor(80, 53); tft.print(temperaturaActual, 1); tft.print("C");
  tft.setTextSize(1); tft.setTextColor(t.textoPrincipal);
  tft.setCursor(180, 58); tft.print("Meta:"); tft.setTextColor(t.textoAcento);
  tft.print(temperaturaObjetivo, 1); tft.print("C");
  ultimaTempMostrada = temperaturaActual;

  tft.setTextSize(1); tft.setCursor(15, 82);
  if      (modoOperacion == 0) { tft.setTextColor(0x8410);        tft.print("MODO: MANUAL"); }
  else if (modoOperacion == 1) { tft.setTextColor(t.activoON);    tft.print("MODO: TERMOSTATO"); }
  else                         { tft.setTextColor(ILI9341_YELLOW);tft.print("MODO: CICLO AUTO"); }
  if (cicloPausado) { tft.setTextColor(0xFD20); tft.print(" [PAUSADO]"); }

  tft.fillRect(15, 92, 290, 70, t.panel);
  dibujarMarcoTematico(15, 92, 290, 70);

  auto indRele = [&](int lx, int ly, const char* lbl, bool est, int bx, int by) {
    tft.setTextColor(t.textoPrincipal); tft.setCursor(lx, ly); tft.print(lbl);
    tft.fillRect(bx, by, 45, 20, est ? t.activoON : t.activoOFF);
    dibujarMarcoTematico(bx, by, 45, 20);
    tft.setTextColor(t.textoPrincipal); tft.setCursor(bx + 10, by + 6); tft.print(est ? "ON" : "OFF");
  };
  indRele( 25,  99, "Sumi:", estadoSuministro, 125,  95);
  indRele(185,  99, "Rec:", estadoRecirc,    235,  95);
  indRele( 25, 132, "Vent:", estadoVentilador, 125, 128);
  indRele(185, 132, "Comp:", estadoCompresor,  235, 128);

  tft.setTextSize(1); tft.setTextColor(0x8410);
  tft.setCursor(20, 158); tft.print("LIGHT=Emergencia  HORN=Pausa/Reanuda");
}

void actualizarValoresDinamicos() {
  if (pantallaActual == 0 && !enSubmenuSlot && !animacionActiva && !enEmergencia &&
      temperaturaActual != ultimaTempMostrada) {
    tft.fillRect(80, 53, 95, 18, t.panel);
    tft.setTextSize(2); tft.setTextColor(t.textoAcento);
    tft.setCursor(80, 53); tft.print(temperaturaActual, 1); tft.print("C");
    ultimaTempMostrada = temperaturaActual;
    dibujarBarraSuperior();
  }
}

void dibujarControlTemperatura() {
  tft.setTextColor(t.textoAcento); tft.setTextSize(1);
  tft.setCursor(15, 30);
  if      (temaEstetico == 0) tft.print(">> SETPOINT DOOM <<");
  else if (temaEstetico == 1) tft.print("* CONTROL TERMICO PIP-BOY *");
  else if (temaEstetico == 2) tft.print("~ PIEDRA SHEIKAH TEMP ~");
  else                        tft.print("[ REGULADOR SAMUS ]");

  tft.fillRect(15, 45, 290, 60, t.panel);
  dibujarMarcoTematico(15, 45, 290, 60);
  tft.setTextColor(t.textoPrincipal); tft.setCursor(25, 55); tft.print("Actual:");
  tft.setTextSize(2); tft.setTextColor(t.textoAcento);
  tft.setCursor(25, 72); tft.print(temperaturaActual, 1); tft.print(" C");
  tft.setTextSize(1); tft.setTextColor(t.textoPrincipal);
  tft.setCursor(160, 55); tft.print("Meta:");
  tft.setTextSize(2); tft.setTextColor(t.textoAcento);
  tft.setCursor(160, 72); tft.print(temperaturaObjetivo, 1); tft.print(" C");

  tft.fillRect( 15, 120, 135, 40, t.activoOFF);
  dibujarMarcoTematico(15, 120, 135, 40);
  tft.setTextColor(t.textoPrincipal); tft.setTextSize(1); tft.setCursor(45, 135); tft.print("- 0.5 C");

  tft.fillRect(170, 120, 135, 40, t.activoON);
  dibujarMarcoTematico(170, 120, 135, 40);
  tft.setCursor(205, 135); tft.print("+ 0.5 C");

  tft.fillRect(15, 170, 290, 28, t.panel);
  dibujarMarcoTematico(15, 170, 290, 28);
  tft.setCursor(25, 180); tft.print("Modo Termostato: ");
  tft.setTextColor(modoOperacion == 1 ? t.activoON : t.activoOFF);
  tft.print(modoOperacion == 1 ? "ACTIVADO" : "APAGADO");
}

void dibujarRelojAlarma() {
  tft.setTextColor(t.textoAcento); tft.setTextSize(1);
  tft.setCursor(8, 28);
  if      (temaEstetico == 0) tft.print("// RELOJ DOOM  [A=campo UP/DN=valor]");
  else if (temaEstetico == 1) tft.print("* CRONOMETRO PIP-BOY  [A=sel]");
  else if (temaEstetico == 2) tft.print("~ RELOJ HYRULE  [A=campo]");
  else                        tft.print("[ CRONOMETRO VISOR  A=sel ]");

  tft.fillRect(0, 44, 320, 52, t.panel);
  dibujarMarcoTematico(0, 44, 320, 52);
  tft.setTextColor(t.textoPrincipal); tft.setCursor(5, 50); tft.print("SISTEMA:");

  bool hSisAct = (campoSeleccionado == 1), mSisAct = (campoSeleccionado == 2);
  tft.fillRect(15, 58, 55, 28, t.panel);
  tft.drawRect(15, 58, 55, 28, hSisAct ? ILI9341_YELLOW : t.borde);
  tft.setTextSize(2); tft.setTextColor(hSisAct ? ILI9341_YELLOW : t.textoAcento);
  tft.setCursor(18, 64); if (hora12(horaActual) < 10) tft.print("0"); tft.print(hora12(horaActual));

  tft.setTextColor(t.textoAcento); tft.setCursor(72, 64); tft.print(":");

  tft.fillRect(78, 58, 55, 28, t.panel);
  tft.drawRect(78, 58, 55, 28, mSisAct ? ILI9341_YELLOW : t.borde);
  tft.setTextSize(2); tft.setTextColor(mSisAct ? ILI9341_YELLOW : t.textoAcento);
  tft.setCursor(82, 64); if (minutoActual < 10) tft.print("0"); tft.print(minutoActual);

  tft.setTextColor(0x8410); tft.setTextSize(1); tft.setCursor(137, 70);
  tft.print(":"); if (segundoActual < 10) tft.print("0"); tft.print(segundoActual);

  tft.fillRect(168, 58, 52, 28, t.panel);
  dibujarMarcoTematico(168, 58, 52, 28);
  tft.setTextSize(2); tft.setTextColor(esAm(horaActual) ? ILI9341_CYAN : ILI9341_YELLOW);
  tft.setCursor(172, 64); tft.print(esAm(horaActual) ? "AM" : "PM");
  tft.setTextColor(0x8410); tft.setTextSize(1); tft.setCursor(228, 66); tft.print("[A=tog]");

  tft.fillRect(0, 100, 320, 64, t.panel);
  dibujarMarcoTematico(0, 100, 320, 64);
  tft.setTextColor(t.textoPrincipal); tft.setTextSize(1); tft.setCursor(5, 108); tft.print("ALARMA:");

  bool hAlmAct = (campoSeleccionado == 3), mAlmAct = (campoSeleccionado == 4);

  tft.fillRect(15, 120, 55, 28, t.panel);
  tft.drawRect(15, 120, 55, 28, hAlmAct ? ILI9341_YELLOW : t.borde);
  tft.setTextSize(2); tft.setTextColor(hAlmAct ? ILI9341_YELLOW : t.textoAcento);
  tft.setCursor(18, 126); if (hora12(horaAlarma) < 10) tft.print("0"); tft.print(hora12(horaAlarma));

  tft.setTextColor(t.textoAcento); tft.setCursor(72, 126); tft.print(":");

  tft.fillRect(78, 120, 55, 28, t.panel);
  tft.drawRect(78, 120, 55, 28, mAlmAct ? ILI9341_YELLOW : t.borde);
  tft.setTextSize(2); tft.setTextColor(mAlmAct ? ILI9341_YELLOW : t.textoAcento);
  tft.setCursor(82, 126); if (minutoAlarma < 10) tft.print("0"); tft.print(minutoAlarma);

  tft.fillRect(142, 120, 48, 28, t.panel);
  dibujarMarcoTematico(142, 120, 48, 28);
  tft.setTextSize(2); tft.setTextColor(esAm(horaAlarma) ? ILI9341_CYAN : ILI9341_YELLOW);
  tft.setCursor(146, 126); tft.print(esAm(horaAlarma) ? "AM" : "PM");

  tft.fillRect(194, 120, 116, 28, alarmaActiva ? t.activoON : t.activoOFF);
  dibujarMarcoTematico(194, 120, 116, 28);
  tft.setTextSize(1); tft.setTextColor(ILI9341_WHITE);
  tft.setCursor(215, 132); tft.print(alarmaActiva ? "ALARMA ON" : "ALARMA OFF");

  tft.setTextColor(0x8410); tft.setTextSize(1);
  tft.setCursor(5, 170); tft.print("A:H.ALM/M.ALM activa campo  UP/DN=ajusta");
}
void dibujarInterfazSlots() {
  tft.setTextColor(t.textoAcento); tft.setTextSize(1);
  tft.setCursor(15, 30);
  if      (temaEstetico == 0) tft.print("// MISIONES DOOM //");
  else if (temaEstetico == 1) tft.print("* PROGRAMAS PIP-BOY *");
  else if (temaEstetico == 2) tft.print("~ RECETAS HYRULE ~");
  else                        tft.print("[ SECUENCIAS SAMUS ]");
 for (int i = 0; i < 5; i++) {
    int xPos = 15 + i * 58;
    uint16_t col = (i == slotSeleccionadoConfig) ? t.activoON : t.panel;
    tft.fillRect(xPos, 44, 52, 50, col);
    dibujarMarcoTematico(xPos, 44, 52, 50);
    tft.setTextColor(t.textoAcento); tft.setCursor(xPos + 14, 47); tft.print("#"); tft.print(i + 1);
    tft.setTextColor(t.textoPrincipal); tft.setCursor(xPos + 3, 62);
    String nom = String(slotsMemoria[i].nombreSlot); if (nom.length() > 7) nom = nom.substring(0, 7);
    tft.print(nom); tft.setCursor(xPos + 3, 77);
    tft.print((int)slotsMemoria[i].tempMeta); tft.print("C/");
    tft.print(slotsMemoria[i].duracionMinutos); tft.print("m");
  }

  tft.setTextColor(t.textoAcento); tft.setCursor(15, 100); tft.print("Slot "); tft.print(slotSeleccionadoConfig + 1);
  tft.setTextColor(t.textoPrincipal); tft.setCursor(60, 100); tft.print("  [L/R=cambiar]");

  if (cicloIniciado) {
    unsigned long transcurrido = minutosTranscurridos();
    unsigned long restante = 0;
    if (transcurrido < slotsMemoria[slotActivoEjecucion].duracionMinutos)
      restante = slotsMemoria[slotActivoEjecucion].duracionMinutos - transcurrido;
    tft.setTextColor(ILI9341_YELLOW); tft.setCursor(180, 100);
    tft.print("Restan:"); tft.print(restante); tft.print("m");
    if (cicloPausado) { tft.setTextColor(0xFD20); tft.print(" PAUSA"); }
  }

  tft.fillRect( 15, 110,  65, 35, t.activoOFF); dibujarMarcoTematico( 15, 110,  65, 35);
  tft.setTextColor(t.textoPrincipal); tft.setCursor(25, 122); tft.print("- Temp");

  tft.fillRect( 90, 110,  65, 35, t.activoON);  dibujarMarcoTematico( 90, 110,  65, 35);
  tft.setCursor(100, 122); tft.print("+ Temp");

  tft.fillRect(165, 110, 140, 35, t.panel);      dibujarMarcoTematico(165, 110, 140, 35);
  tft.setCursor(185, 122); tft.print("CONFIG SLOT");

  bool enCiclo = cicloIniciado && !cicloPausado;
  uint16_t colCiclo = cicloPausado ? 0xFD20 : (enCiclo ? t.activoOFF : t.activoON);
  tft.fillRect(15, 155, 290, 38, colCiclo); dibujarMarcoTematico(15, 155, 290, 38);
  tft.setTextColor(t.textoPrincipal); tft.setCursor(50, 168);
  if      (!cicloIniciado)  tft.print("INICIAR CICLO AUTOMATICO");
  else if (cicloPausado)    tft.print("CICLO PAUSADO  HORN=reanudar");
  else                      tft.print("DETENER CICLO");
}

void dibujarPantallaQR() {
  tft.setTextColor(t.textoAcento); tft.setTextSize(1);
  tft.setCursor(15, 30);
  if      (temaEstetico == 0) tft.print("// RADIO DOOM //");
  else if (temaEstetico == 1) tft.print("* RADIO PIP-BOY *");
  else if (temaEstetico == 2) tft.print("~ PIEDRA SHEIKAH BLE ~");
  else                        tft.print("[ ENLACE CHOZO ]");

  tft.fillRect(40, 45, 240, 120, t.panel);
  dibujarMarcoTematico(40, 45, 240, 120);
  tft.setTextColor(t.textoAcento); tft.setTextSize(2);
  tft.setCursor(70, 58); tft.print("BLE DEVICE");
  tft.setTextSize(1); tft.setTextColor(t.textoPrincipal);
  tft.setCursor(55,  88); tft.print("Nombre: TermostatoGame");
  tft.setCursor(55, 105); tft.print("UP/DOWN=cursor  A=OK");
  tft.setCursor(55, 120); tft.print("LIGHT=Emergencia");
  tft.setCursor(55, 135); tft.print("HORN=Pausa/Apaga/Emergencia");

  tft.fillRect(40, 175, 240, 25, dispositivoConectado ? t.activoON : t.activoOFF);
  dibujarMarcoTematico(40, 175, 240, 25);
  tft.setTextColor(t.textoPrincipal); tft.setCursor(90, 182);
  tft.print(dispositivoConectado ? "[ CONECTADO ]" : "[ ESPERANDO... ]");
}
// LÓGICA MODOS
void ejecutarLogicaModos() {
  if (modoOperacion == 1) {
    float h = 0.5;
    if      (temperaturaActual > (temperaturaObjetivo + h))
      { estadoCompresor = true;  estadoVentilador = true;  estadoSuministro = true; }
    else if (temperaturaActual <= temperaturaObjetivo)
      { estadoCompresor = false; estadoVentilador = false; estadoSuministro = false; }
    aplicarReles(); return;
  }
  if (modoOperacion == 2 && cicloIniciado && !cicloPausado) {
    SlotCiclo &s = slotsMemoria[slotActivoEjecucion]; float h = 0.5;
    if      (temperaturaActual > (s.tempMeta + h))
      { estadoCompresor = s.relCompresor; estadoVentilador = s.relVentilador;
        estadoSuministro = s.relSuministro; estadoRecirc = s.relRecirc; }
    else if (temperaturaActual <= s.tempMeta)
      { estadoCompresor = false; estadoVentilador = false; estadoSuministro = false; estadoRecirc = false; }
    aplicarReles();
    unsigned long mins = minutosTranscurridos();
    if (mins >= s.duracionMinutos) {
      if (slotActivoEjecucion < 4) {
        slotActivoEjecucion++; tiempoInicioSlot = millis(); tiempoAcumuladoSlot = 0;
        Serial.print("Slot -> "); Serial.println(slotActivoEjecucion + 1);
      } else {
        apagarTodo(); iniciarAnimacion(2);
      }
    }
  }
}

void aplicarReles() {
  escribirRele(RELAY_COMPRESOR,  estadoCompresor);
  escribirRele(RELAY_VENTILADOR, estadoVentilador);
  escribirRele(RELAY_SUMINISTRO, estadoSuministro);
  escribirRele(RELAY_RECIRC,     estadoRecirc);
}
// Solo apaga los relés (no toca modos ni el estado del ciclo)
void apagarReles() {
  estadoSuministro = false; estadoRecirc = false;
  estadoVentilador = false; estadoCompresor = false;
  escribirRele(RELAY_SUMINISTRO, false); escribirRele(RELAY_RECIRC,    false);
  escribirRele(RELAY_VENTILADOR, false); escribirRele(RELAY_COMPRESOR, false);
}
// Apaga relés y reinicia 
void apagarTodo() {
  apagarReles();
  modoOperacion = 0; cicloIniciado = false; cicloPausado = false;
  campoSeleccionado = 0; enEmergencia = false;
}