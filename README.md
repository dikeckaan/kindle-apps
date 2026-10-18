# Kindle Apps (TR)

Jailbreak'li Kindle'lar için **Kindle ana ekranından açılan**, e-ink'i yormayan, Türkçe uygulamalar.
Kitaplıkta tek bir **"Uygulamalar"** girişi olur; açınca bütün uygulamaların menüsü gelir.

- Hamlelerde ekran **yanıp sönmez**: yalnızca değişen bölgeler flaşsız yenilenir, tam yenileme nadiren yapılır.
- Uygulama açıkken Kindle arayüzü duraklatılır; uygulama çökse bile arayüz her zaman geri gelir.
- Her şey yalnızca `/mnt/us` altına kurulur; sistem dosyalarına dokunulmaz.

## Uygulamalar

| Uygulama | Durum |
|---|---|
| 2048 | ✅ |
| Satranç (Stockfish'e karşı 6 zorluk, iki kişilik) | ✅ |
| Uyku Ayarı (uyku süresini uzat / kapat) | ✅ |
| SSH Sunucusu (parolalı, menüden aç/kapat) | ✅ |
| Güvenlik Duvarı (aç/kapat) | ✅ |
| Kelime Oyunu (Türkçe Wordle: 5 harf, yalnızca kök kelimeler, sınırsız) | ✅ |
| Hava Durumu (Open-Meteo, kayıtlı şehirler, 12 saat + 7 gün) | ✅ |
| Masa Saati (büyük saat, tarih, hava özeti; açıkken uyumaz) | ✅ |
| Masa Panosu (saat, hava, seçilen kaynaktan son dakika, döviz/altın, tarihte bugün, namaz vakitleri, ay evresi, pil) | ✅ |
| Gökyüzü (çevrimdışı canlı yıldız haritası: yıldızlar, takımyıldızlar, gezegenler, Ay evresi, gün doğumu/batımı) | ✅ |
| Döviz & Altın (TCMB kurları, altın fiyatları, çevirici) | ✅ |
| Gazete (20 kaynaktan gazete tadında ön sayfa: AA, TRT, Hürriyet, Sözcü, NTV, CNN Türk, BBC Türkçe, Cumhuriyet, Milliyet, Sabah, Habertürk, DW, Euronews…) | ✅ |
| Mayın Tarlası (3 zorluk, bayrak kipi) | ✅ |
| Sudoku (3 zorluk, tek çözümlü, not kipi, ipucu) | ✅ |
| Solitaire (Klondike, 1 / 3 kart çekme, geri al) | ✅ |
| Sistem Bilgisi (pil, depolama, bellek, Wi-Fi, sürüm, güvenlik) | ✅ |
| Wi-Fi ile Yükle (tarayıcıdan PIN'li kitap gönderme) | ✅ |
| E-Kitap (Project Gutenberg'den telifsiz kitap arama ve indirme, AZW3/EPUB) | ✅ |

## Gereksinimler

- Jailbreak'li, **kindlehf** (FW ≥ 5.16.3) bir Kindle; KMC araçları ve `kpm` kurulu
  (bkz. [kindlemodding.org](https://kindlemodding.org)). Kindle Basic 11. nesil (FW 5.19.2) üzerinde test edildi.
- Uygulamalar açılırken **KOReader kapalı olmalı** (dokunmatik ekranı o kullanır).

## Kurulum

**Kolay yol:** [Releases](https://github.com/dikeckaan/kindle-apps/releases) sayfasından `kindle-apps-depo.sh`
dosyasını indirip USB ile Kindle'ın `documents` klasörüne kopyalayın, kitaplıkta **"Kindle Apps Deposu"**na dokunun.

**SSH ile:**

```sh
/var/local/kmc/bin/kpm add-repo https://dikeckaan.github.io/kindle-apps/manifest.json
/var/local/kmc/bin/kpm update
/var/local/kmc/bin/kpm -y install kindle-apps-core   # önce çekirdek
/var/local/kmc/bin/kpm -y install kindle-apps-2048   # sonra uygulamalar, birer birer
/var/local/kmc/bin/kpm -y install kindle-apps-chess
/var/local/kmc/bin/kpm -y install kindle-apps-sleep
/var/local/kmc/bin/kpm -y install kindle-apps-ssh
/var/local/kmc/bin/kpm -y install kindle-apps-firewall
/var/local/kmc/bin/kpm -y install kindle-apps-weather
/var/local/kmc/bin/kpm -y install kindle-apps-clock
/var/local/kmc/bin/kpm -y install kindle-apps-currency
/var/local/kmc/bin/kpm -y install kindle-apps-news
/var/local/kmc/bin/kpm -y install kindle-apps-wordle
/var/local/kmc/bin/kpm -y install kindle-apps-mines
/var/local/kmc/bin/kpm -y install kindle-apps-sudoku
/var/local/kmc/bin/kpm -y install kindle-apps-solitaire
/var/local/kmc/bin/kpm -y install kindle-apps-sysinfo
/var/local/kmc/bin/kpm -y install kindle-apps-upload
/var/local/kmc/bin/kpm -y install kindle-apps-ebooks
/var/local/kmc/bin/kpm -y install kindle-apps-pano
```

Ardından kitaplıkta **Uygulamalar**'ı açın.

### SSH Sunucusu

İlk açılışta ekran klavyesiyle bir parola belirlersiniz (en az 6 karakter). **Aç**'a basınca ekranda
`ssh root@<ip>` komutu görünür; sunucu uygulamayı kapatsanız da çalışır, **Kapat** ile durur. Parola yalnızca
root'un okuyabildiği `/var/local/kindle-apps/ssh` içinde tuzlu ve 10 000 tur SHA-256 ile saklanır.

## Kaldırma

```sh
/var/local/kmc/bin/kpm uninstall kindle-apps-2048 kindle-apps-chess kindle-apps-sleep kindle-apps-ssh kindle-apps-firewall kindle-apps-weather kindle-apps-clock kindle-apps-currency kindle-apps-news kindle-apps-wordle kindle-apps-mines kindle-apps-sudoku kindle-apps-solitaire kindle-apps-sysinfo kindle-apps-upload kindle-apps-ebooks kindle-apps-pano kindle-apps-core
```

Oyun kayıtları ve ayarlar `/mnt/us/kindle-apps/data/` içinde kalır; tamamen silmek için o klasörü de silin.

### Dil

Uygulamalar menüsünde sağ üstteki **Dil** düğmesiyle 20 dilden biri seçilir: Türkçe, English, Deutsch, Français,
Español, Italiano, Português, Nederlands, Polski, Čeština, Română, Magyar, Svenska, Norsk bokmål, Dansk, Suomi,
Ελληνικά, Русский, Українська, Azərbaycanca. Kelime Oyunu Türkçe kelimelerle oynanır; Gazete ve Döviz Türk kaynaklarını
kullanır. Çeviriler `lang/<kod>.txt` dosyalarındadır; `tools/i18n/extract.py --check` eksikleri gösterir.

### İnternetli uygulamalar

Hava Durumu, Masa Saati, Döviz & Altın ve Gazete Wi-Fi ile veri çeker; bağlantı yoksa son indirilen veriyi
zamanıyla gösterir. Veri kaynakları: [Open-Meteo](https://open-meteo.com) (CC BY 4.0), TCMB günlük kurları,
Truncgil Finans (altın) ve haber kuruluşlarının herkese açık RSS/Atom akışları. Gazete yalnızca RSS'te yayımlanan metni ve haberin
bağlantısını gösterir; görsel indirmez.

### Wi-Fi ile Yükle

Uygulamayı açınca ekranda `http://<ip>:8080` adresi ve 6 haneli bir PIN görünür. Aynı Wi-Fi'daki telefon ya da bilgisayarın
tarayıcısında bu adresi açın, PIN'i girip kitapları seçin; dosyalar `documents` klasörüne gelir. Sunucu yalnızca uygulama
açıkken çalışır; çok sayıda yanlış PIN denemesinde kendini kilitler.

## Sorun giderme

- Açılmıyorsa günlüğe bakın: `/mnt/us/kindle-apps/logs/<uygulama>.log`.
- "Dokunmatik meşgul" yazıyorsa KOReader'ı kapatın.
- Ekran donarsa güç düğmesini ~40 sn basılı tutarak Kindle'ı yeniden başlatın (hiçbir şey bozulmaz).

## Geliştirme

```sh
docker build -t kindle-apps-tc tools/docker   # kindlehf araç zinciri
./tools/fetch-deps.sh                          # FBInk + DejaVu fontları
make test                                      # host birim testleri
make build dist                                # cihaz ikilileri + paketler
make deploy KINDLE=192.168.2.90                # SSH ile cihaza kur
tools/sim/run.sh weather "tap 970 60"          # cihazsız deneme: ekranlar build/sim/out/ altında PNG
```

Tasarım: `docs/superpowers/specs/`, planlar: `docs/superpowers/plans/`.

## Lisans

GPLv3 — bkz. `LICENSE` ve `THIRD_PARTY.md` (FBInk GPLv3+, Stockfish GPLv3, Dropbear MIT, DejaVu fontları).
