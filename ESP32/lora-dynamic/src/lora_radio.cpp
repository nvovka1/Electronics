#include <SPI.h>
#include <LoRa.h>
#include "config.h"
#include "lora_radio.h"

bool startLoraRadio() {
  // LORA_SCK/MISO/MOSI/CS/RST/IRQ come from the ttgo-lora32-v21 board definition.
  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_CS);
  LoRa.setPins(LORA_CS, LORA_RST, LORA_IRQ);
  if (!LoRa.begin(LORA_FREQUENCY_HZ)) return false;
  LoRa.setSpreadingFactor(LORA_SPREADING_FACTOR);
  LoRa.setSignalBandwidth(LORA_BANDWIDTH_HZ);
  LoRa.setSyncWord(LORA_SYNC_WORD);
  LoRa.setTxPower(LORA_TX_POWER_DBM);
  LoRa.enableCrc();  // a damaged packet is dropped instead of played as a squeal
  return true;
}
