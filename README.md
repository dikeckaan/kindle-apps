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
| Türkçe Wordle | yakında |
| Hava durumu (Open-Meteo), Masa saati, Döviz & Altın (TCMB), Gazete (AA / TRT Haber) | yakında |
| Mayın Tarlası, Solitaire, Sudoku | yakında |
| Sistem bilgisi, Wi-Fi ile dosya yükleme | yakında |

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
```

Ardından kitaplıkta **Uygulamalar**'ı açın.

### SSH Sunucusu

İlk açılışta ekran klavyesiyle bir parola belirlersiniz (en az 6 karakter). **Aç**'a basınca ekranda
`ssh root@<ip>` komutu görünür; sunucu uygulamayı kapatsanız da çalışır, **Kapat** ile durur. Parola yalnızca
root'un okuyabildiği `/var/local/kindle-apps/ssh` içinde tuzlu ve 10 000 tur SHA-256 ile saklanır.

## Kaldırma

```sh
/var/local/kmc/bin/kpm uninstall kindle-apps-2048 kindle-apps-chess kindle-apps-sleep kindle-apps-ssh kindle-apps-firewall kindle-apps-core
```

Oyun kayıtları ve ayarlar `/mnt/us/kindle-apps/data/` içinde kalır; tamamen silmek için o klasörü de silin.

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
```

Tasarım: `docs/superpowers/specs/`, planlar: `docs/superpowers/plans/`.

## Lisans

GPLv3 — bkz. `LICENSE` ve `THIRD_PARTY.md` (FBInk GPLv3+, Stockfish GPLv3, Dropbear MIT, DejaVu fontları).
