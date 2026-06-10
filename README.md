# Masa Bilgi Ekranı

ESP32 + TFT_eSPI ekran ile Moonraker/Klipper yazıcı durumunu izleyen Arduino projesi.

## Özellikler

- Hotend ve yatak sıcaklıklarını hedef değerleriyle birlikte gösterir.
- Baskı ilerlemesini ve tahmini kalan süreyi hesaplar.
- Dosya adını, yazıcı durumunu ve WiFi/IP bilgisini ekranda gösterir.
- Kart tabanlı, daha okunaklı ve renkli bir arayüz kullanır.
- Tüm ekranı sürekli silmek yerine sadece değişen alanları güncelleyerek titreme/yanıp sönmeyi azaltır.
- WiFi koparsa bloklamadan tekrar bağlanmayı dener.
- Moonraker erişilemezse yazıcı ekranından masa saati ekranına geçer.
- Yazıcı yeniden erişilebilir olduğunda otomatik olarak yazıcı paneline döner.
- Moonraker HTTP veya JSON hatalarını ekranda kısa durum mesajı olarak gösterir.

## Kurulum

1. Arduino IDE veya PlatformIO'da ESP32 kart desteğini kurun.
2. Saat/tarih için ESP32 internete çıkabilmeli; varsayılan saat dilimi Türkiye için UTC+3 olarak ayarlanmıştır.
3. Aşağıdaki kütüphaneleri yükleyin:
   - `TFT_eSPI`
   - `ArduinoJson`
4. `MasaBilgiEkran/MasaBilgiEkran.ino` içindeki `ssid`, `password`, `moonrakerIP` ve gerekirse NTP saat dilimi değerlerini kendi ağınıza göre değiştirin.
5. `TFT_eSPI` ayarlarınızda ekran sürücünüzün ve pinlerinizin doğru tanımlandığından emin olun.
6. ESP32'ye yükleyin.
