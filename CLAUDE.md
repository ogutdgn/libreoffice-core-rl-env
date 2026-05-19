# CLAUDE.md

> Bağlam dosyası: bu repo'da çalışan Claude Code (ve diğer Anthropic
> Claude tabanlı ajanlar) için. Bu dosya **kısa**. Tüm gerçek proje
> rehberi [`AGENTS.md`](AGENTS.md)'de.

---

## Önce bunu yap

1. **[`AGENTS.md`](AGENTS.md) dosyasını oku.** Tüm proje bağlamı,
   workflow, build adımları, conventional commits, bilinen gotcha'lar
   orada.
2. Sonra göreve dön.

---

## Proje tek cümleyle

**LibreOffice fork'u**, Writer + Calc + Impress odaklı, RL agent
ortamı için sadeleştiriliyor. Sahibi `dev` branch'inde UI editleme
çalışmaları yapacak. Disiplin: **build-driven** — her değişiklik
build doğrulamasından geçer, plan-driven yaklaşım terk edildi.

---

## Mevcut durum: Phase 3 logger V1.1 entegre

`dev` branchine merge edilmiş bir `rllogger/` modülü var. Soffice'i
açtığın an arka planda her şeyi loglar — Writer için key/mouse, her
`.uno:*` dispatch (args + trigger + gesture range), her 250 ms'de bir
doküman snapshot (cursor + selection + format-at-cursor).

- **Default'ta açık**, env var gerekmez. Loglar `~/.lo-rl-logs/<sessionId>/`'a yazılır
- `LO_RL_LOG_DIR=/path` ile yönlendirilebilir (CI / test için)
- `LO_RL_LOG_DISABLE=1` zero-overhead opt-out
- Tüketici tarafı: `rllogger/util/rllogger-export.py <sessionDir> -o out.json` → tek paket session.json

Tam sözleşme: [`AGENTS.md`](AGENTS.md) §4.3.
Tasarım + verification: [`docs/architecture/PHASE3_LOGGER_DESIGN.md`](docs/architecture/PHASE3_LOGGER_DESIGN.md).

Logger-ilgili değişiklik yapıyorsan tipik smoke test:

```sh
rm -rf /tmp/rl-test && LO_RL_LOG_DIR=/tmp/rl-test \
  instdir/program/soffice --writer --norestore
# Writer'da bir kaç komut çalıştır, kapat
SESSION=$(ls -t /tmp/rl-test | head -1)
cat /tmp/rl-test/$SESSION/semantic.jsonl
cat /tmp/rl-test/$SESSION/outcome.jsonl
```

`make rllogger desktop` ile incremental build yeterli (sofficemain
rllogger'a link olduğu için desktop da yeniden link gerekir; sw/sc/sd
değil).

---

## Claude-spesifik kurallar

### 1. Commit'lerde `Co-Authored-By: Claude` **ASLA** olmayacak

Sahibi bunu açıkça istedi. Default Claude Code commit davranışından
sapma. Footer'a Claude ilgili **hiçbir şey ekleme**. Mesaj sadece
conventional commit body'si.

Eğer kazara böyle bir commit yaparsan: henüz push edilmediyse
`git commit --amend`. Push edildiyse sahibine haber ver, beraber
karar verirsiniz.

### 2. Conventional Commits

Format: `<type>(<scope>): <subject>` — detay [`AGENTS.md`](AGENTS.md) §8.

### 3. Build hataları için disiplin

LibreOffice build hatası gördüğünde:
- **Önce bağlamı oku** — hata mesajının tamamı, hangi adımda, hangi
  modülde.
- **Acele commit etme** — geri alınabilir hâlde tut.
- **Önceki branch'lerde benzer hata var mı bak** — `chore/strip-...`
  ve `refactor/apps-core-folder-split` branch'lerinde yaşanmış olabilir.
- Çözünce: `make sw sc sd` ile incremental rebuild + smoke test.
  Geçerse commit.

### 4. Skill kullanımı

- `superpowers:systematic-debugging` → build/runtime hatası araştırırken
- `superpowers:test-driven-development` → yeni feature/bugfix yazarken
  (CppUnit testleri var: `make sw.check` vb.)
- `superpowers:verification-before-completion` → "tamam, çalışıyor"
  demeden ÖNCE smoke test komutunu gerçekten çalıştır

### 5. Workflow kalıbı (geliştirme döngüsü)

```sh
# Sahibi WSL'de düzenleme yapacaksa ya da Claude push'tan sonra:
git pull origin dev

# Edit (varsa):
vim sw/source/uibase/...

# Build:
make sw sc sd

# Smoke test:
instdir/program/soffice --writer

# Commit + push:
git add -A
git commit -m "feat(sw): ..."
git push origin dev
```

Her döngüde build'in geçtiğini görmeden bir sonraki değişikliği
yapma.

### 6. Lokal ortam farkındalığı

- Claude (Windows): `c:/Users/ogutd/OneDrive/Desktop/new-coding/test-rl/libreoffice-core`
- Sahibi (WSL Ubuntu): `/home/ogutd/lo-dev` (ya da benzeri)
- GitHub: source of truth — ikisi de buradan pull/push yapar
- Build'ler **WSL Linux fs'te yapılır**, /mnt/c yavaş

Claude `wsl -e bash -lc "..."` ile WSL içine komut gönderebilir.
Komutlardaki PATH'ten mingw64'ü çıkar yoksa configure WSL'yi
"wsl-as-helper" build sanır:

```sh
export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
```

### 7. Memory ile karışıklık

Bu dosyada olan kuralları memory'ye tekrar yazma. Memory'de
tutmaman gereken şeyler:
- Bu dosyalarda zaten yazılı olan workflow / convention bilgisi
- Geçmiş plan dokümanlarındaki kararlar (chore/strip... ve
  refactor/apps-core-folder-split'te var)

Memory'de tutman gereken şeyler:
- Sahibinin değişen tercihleri / yeni feedback'leri
- Bu dosyalarda olmayan, ileride faydalı olabilecek bağlam

---

## Kullanıcı hakkında

- Tek kişilik proje sahibi (`@ogutdgn`).
- Türkçe + İngilizce karışık konuşur; Türkçe sorulara Türkçe yanıt ver.
- Mühendislik kararlarında **tradeoff'ları açıkça** ister, blind
  agreement değil.
- "Beni ikna etmeye çalışma" demişti — gerekirse karşı görüş bildir.
- Uzun analiz dokümanlarına dayanıklı (örn.
  [`docs/architecture/WRITER_CALC_EXTRACTION.md`](docs/architecture/WRITER_CALC_EXTRACTION.md)).
- "Authored by Claude" gibi AI-attribution ifadelerinden hoşlanmaz.

---

## Önceki çalışma — hatırlatma

İki paused branch var GitHub'da:

- `chore/strip-to-writer-calc-impress` — 16 commit, ~5900 dosya silindi
  (peer apps, language bridges, mobile, vs). Ama build doğrulanmadı,
  manifest fix'leri eksik kalmış olabilir. **Cherry-pick kullanma**
  — gözden geçirmeden referans almak yeterli.
- `refactor/apps-core-folder-split` — `chore/strip...` üzerine ek 6
  commit, sw/sc/sd `apps/`'a, ~91 modül `core/`'a taşındı. Build
  tamamen yeşilleştirilemedi (suppression dosyaları, stale workdir
  vs). Aynı şekilde **cherry-pick yok, sadece referans**.

`dev` branch'te bu işleri **incremental ve build-verified** şekilde
yeniden yapacağız. Plan: [`AGENTS.md`](AGENTS.md) §4.
