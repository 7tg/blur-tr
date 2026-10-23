# blur-tr

PC Blur (2010, Bizarre Creations / Activision) için Türkçe yama.

**Hiçbir Activision içeriği yeniden dağıtılmaz.** Paketin tek dağıttığı şey:
küçük bir ASI eklentisi (oyunun kendi lokalizasyon ayrıştırıcısına bellek
içinde takılan bir hook) ve yazarın oyunun metin tablolarını kendisi
Türkçeye çevirdiği metinler. Oyunun fontları, sesleri, çalıştırılabilir
dosyaları, görselleri, PAK arşivleri ve `.sig` imzaları bu depoda yoktur,
hiçbirine dokunulmaz.

Bu yamayı kullanmak için **PC Blur'un yasal bir kopyasına sahip olmanız
gerekir.** Ayrıntılar için `DISCLAIMER.md` dosyasına bakın.

## Kurulum

```sh
tools/install.sh "/path/to/Blur game folder"
```

Örnek (Wine üzerinde Linux):

```sh
tools/install.sh "/home/you/.wine/drive_c/Program Files (x86)/Activision/Blur"
```

Kurulum, Blur klasörünüze yalnızca iki şey yazar:

```
scripts/blur_tr.asi
scripts/blur-tr-oltx/*.oltx       (25 dosya)
```

Yüklenebilmesi için Blur klasöründe **Ultimate ASI Loader** (herhangi güncel
bir sürüm) bulunmalıdır — loader'ın DLL'ini `dsound.dll` veya `dinput8.dll`
olarak yeniden adlandırıp `Blur.exe`'nin yanına koyun. Oyun başlarken
loader'ı yükler, loader da `scripts/blur_tr.asi`'yi otomatik olarak alır.

Hiçbir dil işareti dosyası (`english.txt` / `polish.txt`) oluşturulmaz veya
silinmez. `gamedata/` altındaki hiçbir dosyaya dokunulmaz. Oyunun **sesli
içeriği İngilizce kalır**, yalnızca metinler Türkçeleşir.

Geri almak için:

```sh
tools/uninstall.sh "/path/to/Blur game folder"
```

## Derleme

Paket, önceden derlenmiş OLTX ikili dosyalarını `data/oltx-tr/` altında
beraberinde getirir. Kaynaktan yeniden derlemek için:

```sh
# data/translated-tr-in-en/*.json -> data/oltx-tr/*.oltx kodlar ve
# src/asi/blur_tr.asi'yi derler
tools/build_pack.sh
```

ASI derlemesi için `i686-w64-mingw32` araç zinciri gerekir (Arch'ta
`sudo pacman -S mingw-w64-gcc`, diğer dağıtımlarda eşdeğeri).

Çevirinin kendisini düzenlemek için `data/translated-tr-in-en/*.json`
dosyalarını değiştirip `tools/build_pack.sh`'i tekrar çalıştırın.

## Nasıl Çalışır

Blur'un metin tabloları, oyunun PAK arşivleri içine gömülü OLTX paketlerinde
taşınır. Her paket aynı anahtar kümesini birkaç dil yuvası altında tutar
(`en`, `fr`, `de`, `it`, `es`) ve motor hangi yuvayı okuyacağına, iki
harfli dil kodunu yuva adıyla karşılaştırarak karar verir (büyük/küçük
harfe duyarsız). İngilizce bir kurulumda motor `en` yuvasını seçer.

`blur_tr.asi`, motorun OLTX akış ayrıştırıcısına hook atar. Blur, bu
yamanın kapsadığı paketlerden birini ayrıştırdığında ASI o paketi içerik
parmak izi ile tanır (`num_keys`, en küçük anahtar hash'i), ayrıştırıcının
tamponlarını serbest bırakır ve motora yamanın kendi OLTX baytlarını
verir. Motor, Türkçe metinleri yama paketinin `en` yuvasından sanki
orijinal İngilizce metinmiş gibi okur — dil değişikliği yok, font
değişikliği yok, exe yaması yok, `.sig` müdahalesi yok.

Motorun İngilizce fontu Latin-1 karakter setini kullandığı için `ıİğĞşŞ`
karakterlerini çizemez. Derleme hattı bu karakterleri ASCII karşılıklarına
indirger (`iIgGsS`). Latin-1'de zaten bulunan Türkçe karakterler
(`çöüÇÖÜ`) olduğu gibi korunur.

Çeviri, 25 OLTX paketinde yaklaşık 5.800 anahtarı kapsar. Biçim belirteçleri,
yer tutucu etiketler ve renk kodları birebir korunur. Özel adlar (araba
markaları, mod isimleri, yer isimleri) sözlüğe göre İngilizce bırakılır.

## Paket İçeriği

```
src/asi/blur_tr.c             -- ASI eklenti kaynağı (make ile derlenir)
src/asi/Makefile              -- MinGW-w64 çapraz derleme
tools/oltx.py                 -- OLTX codec (encode / decode / roundtrip)
tools/transform_translations.py
                              -- 6 yuvalı JSON (Türkçe pl'de) -> en yuvasına
                                 ASCII indirgeme + sözlük uygulamasıyla
tools/build_pack.sh           -- data/oltx-tr/*.oltx + ASI yeniden üretir
tools/install.sh              -- yamayı Blur klasörüne kurar
tools/uninstall.sh            -- install.sh'in yaptıklarını geri alır
data/translated-tr-in-en/     -- 25 JSON metin tablosu (kaynak, git'te)
data/oltx-tr/                 -- JSON'lardan kodlanmış 25 OLTX ikili
docs/vfs_paths.md             -- her paketin içerik alanına dair notlar
LICENSE                       -- Apache-2.0 (kod)
LICENSE-translations          -- CC BY-SA 4.0 (çeviri metinleri)
DISCLAIMER.md                 -- resmi olmayan hayran işi; ticari markalar
CHANGELOG.md
```

## Test Tarifi

1. Blur'un temiz bir kurulumuna Ultimate ASI Loader ekleyin, sonra bu
   yamayı kurun.
2. Wine (32-bit prefix) veya yerel Windows üzerinde çalıştırın.
3. Beklenen: menü metinleri, bildirimler, başarımlar, çok oyunculu ekranlar
   ve araba bilgileri Türkçe; özel adlar İngilizce.
4. Teşhis günlüğü: `<oyun>/scripts/blur_tr.log` dosyası, ASI'nin ayrıştırma
   anında değiştirdiği her OLTX paketini kayıt altına alır. Bir ekran hâlâ
   İngilizce görünüyorsa, orada görünen anahtar isimlerini not edip
   `docs/vfs_paths.md` üzerinden hangi pakete ait olduğunu bulabilirsiniz.

## Hedef Dışı

- Activision içeriklerinin yeniden dağıtımı yoktur (font, ses, görsel,
  exe, PAK arşivleri).
- `.sig` müdahalesi, exe yaması veya ilgisiz süreçlere DLL enjeksiyonu
  yoktur.
- Dil değişikliği yoktur — motor İngilizce modda kalır, biz sadece onun
  İngilizce yuvasına Türkçe metinleri sunarız. Sesli içerik bu nedenle
  İngilizce olarak kalır.

## Teşekkürler

Yıllar önce PC Blur için ilk Türkçe yamayı yapan **kosemen_76**'ya
teşekkürler. Bu depodaki hiçbir kod veya çeviri ondan alınmamış olsa da,
Blur'un Türkçe oynanabilmesinin yolunu ilk açan odur.
