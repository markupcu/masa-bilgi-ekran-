# ESP32 Masa Bilgi Ekrani

Bu proje, 320×240 TFT ekranli bir ESP32'yi Klipper/Moonraker yazici paneli,
muzik bilgi ekrani ve synthwave saat olarak kullanir.

## Eklenen iyilestirmeler

- Moonraker veya Wi-Fi'da anlik kesinti oldugunda aktif baski ekrani 15 saniye
  boyunca korunur; ekran gereksiz yere standby moduna donmez.
- Muzik guncellemesi kesilirse 6 saniye sonra otomatik olarak standby ekranina
  gecilir. Muzik ilerleme cubugu yeni veri gelmese de saniye saniye ilerler.
- `GET /status` endpoint'i Wi-Fi, yazici ve muzik durumunu JSON olarak verir.
- `POST /music` JSON istekleri dogrulanir ve CORS basligi ile yanitlanir.
- Cizim onbellegi degisen alanlari yeniden cizer; ekran titremesi ve gereksiz
  TFT yazmalari azalir.

## Kurulum

1. Arduino IDE veya PlatformIO'da ESP32 kart destegi ile `masa_bilgi_ekran.ino`
   dosyasini acin.
2. **ArduinoJson** ve **TFT_eSPI** kutuphanelerini yukleyin. TFT_eSPI'nin
   `User_Setup` ayarlarini ekraninizin surucusu ve pinlerine gore yapin.
3. Dosyanin basindaki `WIFI_SSID`, `WIFI_PASSWORD`, `MOONRAKER_HOST` ve saat
   dilimi sabitlerini kendi ortaminiza gore duzenleyin.
4. Kodu ESP32'ye yukleyin. Cihaz IP'si standby ekraninin altinda gorunur.

## Muzik verisi gonderme

ESP32'nin IP adresine asagidaki gibi bir istek atabilirsiniz:

```bash
curl -X POST http://ESP32_IP/music \
  -H 'Content-Type: application/json' \
  -d '{"title":"Parca adi","artist":"Sanatci","progress":42000,"duration":210000,"playing":true}'
```

`progress` ve `duration` alanlari milisaniye cinsindendir. Cihazin genel
durumunu `curl http://ESP32_IP/status` ile kontrol edebilirsiniz.
