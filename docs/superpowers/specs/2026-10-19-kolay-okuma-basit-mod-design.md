# Kolay okuma ve Basit mod — tasarım

## Amaç
Uygulamaların gözlüksüz, rahatça okunabilmesi (ölçüt: telefonu gözlükle okuyan biri Kindle'ı gözlüksüz okuyabilsin) ve
teknolojiyle arası iyi olmayan biri için sadeleşmesi. Tasarım baştan değişmez; Uygulamalar menüsünden açılan iki
bağımsız ayar eklenir. Yatay mod sonraya bırakıldı (kullanıcı kararı).

## Ayarlar (Uygulamalar → Ayarlar)
- Üst çubuktaki "Dil" düğmesi **Ayarlar** olur: Dil, **Kolay okuma** (aç/kapat), **Basit mod** (aç/kapat).
- Değerler `data/ui` dosyasında (`easy=0|1`, `simple=0|1`); her uygulama açılışta okur (kui_init).
- Ayarlar ekranı basit modda da her zaman erişilebilir (geri dönüş yolu).

## Kolay okuma (merkezi, kui içinde)
Her metin çizimi şu kuraldan geçer (saf fonksiyon, testli, cihaz ve simülatör aynı kodu kullanır):
- Normal yazı → kalın, tırnaklı → tırnaklı kalın.
- Gri yazı (açık renkli metinler hariç) → tam siyah.
- Boyut ×1.3 (en az 30 px); tek satıra sığan bir metin büyüyünce taşarsa kutuya sığacak kadar küçültülür
  (asla eski boyutun altına inmez); kutu yüksekliğini aşmaz.
- Böylece tüm uygulamalar ek iş olmadan iri ve koyu olur; yerleşim aynı kalır.

## Basit mod (uygulama başına)
- **Uygulamalar menüsü:** sayfada 4 büyük kutucuk (2×2); teknik uygulamalar gizlenir (SSH, Güvenlik Duvarı,
  Sistem Bilgisi, Uyku Ayarı, Wi-Fi ile Yükle, Dikeç Store). Sayfa düğmeleri "Önceki / Sonraki" yazılı.
- **Masa Panosu:** saat, hava ve en fazla 3 haber; döviz, tarihte bugün, namaz vakitleri kapalı.
- Diğer uygulamalar ilk sürümde yalnızca Kolay okumadan yararlanır.

## Kararlar (benim; kullanıcı itiraz edebilir)
- Ölçek ×1.3 ve en az 30 px: cihazda denenip ayarlanabilir (tek sabit).
- Basit modda Dikeç Store da gizli: yanlışlıkla uygulama kaldırma riskine karşı. Ayarlar'dan basit mod kapatılınca geri gelir.
