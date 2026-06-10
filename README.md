# Masa Bilgi Ekranı

ESP32 + TFT_eSPI ekran ile Moonraker/Klipper yazıcı durumunu izleyen Arduino projesi.

## Özellikler

- Hotend ve yatak sıcaklıklarını hedef değerleriyle birlikte gösterir.
- Baskı ilerlemesini ve tahmini kalan süreyi hesaplar.
- Dosya adını, yazıcı durumunu ve WiFi/IP bilgisini ekranda gösterir.
- Kart tabanlı, daha okunaklı ve renkli bir arayüz kullanır.
- Tüm ekranı sürekli silmek yerine sadece değişen alanları güncelleyerek titreme/yanıp sönmeyi azaltır.
- WiFi koparsa bloklamadan tekrar bağlanmayı dener.
- Moonraker HTTP veya JSON hatalarını ekranda kısa durum mesajı olarak gösterir.

## Kurulum

1. Arduino IDE veya PlatformIO'da ESP32 kart desteğini kurun.
2. Aşağıdaki kütüphaneleri yükleyin:
   - `TFT_eSPI`
   - `ArduinoJson`
3. `MasaBilgiEkran/MasaBilgiEkran.ino` içindeki `ssid`, `password` ve `moonrakerIP` değerlerini kendi ağınıza göre değiştirin.
4. `TFT_eSPI` ayarlarınızda ekran sürücünüzün ve pinlerinizin doğru tanımlandığından emin olun.
5. ESP32'ye yükleyin.
