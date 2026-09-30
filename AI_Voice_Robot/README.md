# ESP32-S3 AI 語音視覺聊天機器人（OpenAI 版）

按住按鍵說話 → 放開 → OpenAI 語音轉文字 → GPT 回答（說「拍照／你看到什麼」會附上鏡頭畫面）→ OpenAI TTS 用喇叭講出來，TFT 螢幕顯示表情、時鐘和對話文字。

## 1. 安裝 Arduino IDE 與 ESP32 開發板

1. 安裝 [Arduino IDE 2.x](https://www.arduino.cc/en/software)
2. 檔案 → 偏好設定 → 額外的開發板管理員網址，填入：
   `https://espressif.github.io/arduino-esp32/package_esp32_index.json`
3. 開發板管理員 → 搜尋 **esp32**（by Espressif Systems）→ 安裝 **3.x 版**

## 2. 安裝函式庫（工具 → 管理程式庫）

| 函式庫 | 用途 |
|---|---|
| Adafruit GFX Library | 繪圖基礎 |
| Adafruit ST7735 and ST7789 Library | ST7789 螢幕驅動 |
| Adafruit ILI9341 | ILI9341 螢幕驅動（備用） |
| ArduinoJson（7.x 版） | 解析 OpenAI 回應 |

鏡頭（esp_camera）、I2S（ESP_I2S）、HTTPS 都已內建在 ESP32 開發板套件裡，不用另外安裝。

## 3. 開發板設定（工具選單）

| 項目 | 設定 |
|---|---|
| 開發板 | **ESP32S3 Dev Module** |
| USB CDC On Boot | Disabled（用標示 **TTL** 的 Type-C 口上傳）|
| Flash Size | **16MB (128Mb)** |
| Partition Scheme | **16M Flash (3MB APP/9.9MB FATFS)** |
| PSRAM | **OPI PSRAM**（必選，否則無法錄音與播放）|
| Upload Speed | 921600 |

上傳失敗時：按住 **BOOT** 鍵 → 按一下 **RST** → 放開 BOOT，再上傳一次。

## 4. 修改 `config.h`

1. 填入 `WIFI_SSID`、`WIFI_PASSWORD`（只支援 2.4GHz）
2. 填入 `OPENAI_API_KEY`（到 https://platform.openai.com/api-keys 建立，帳戶要有儲值額度）
3. 腳位已經實測確認，不用改（本套件實際接線）：

   | 元件 | 腳位 |
   |---|---|
   | 2.8" TFT（ST7789） | SCK=3、MOSI=45、CS=14、DC=47、RST=21 |
   | INMP441 麥克風 | SCK=2、WS=1、SD=42（左聲道）|
   | MAX98357A 擴大機 | BCLK=19、LRC=41、DIN=20 |
   | 按鍵 | GPIO0（與 BOOT 鍵並聯，按下為 LOW）|
   | 鏡頭 | OV3660（板載，固定腳位）|

   這些腳位是用 `PinFinder` 系列測試程式實測，再比對原廠韌體反組譯確認的。
   原廠韌體備份在 `backup/original_firmware_16MB.bin`，要還原可執行：
   `esptool --chip esp32s3 --port COM3 write-flash 0 backup/original_firmware_16MB.bin`

## 5. 先跑硬體測試

把 `config.h` 的 `HW_TEST_MODE` 設成 `1`，上傳後打開序列埠監控視窗（115200）：

- 螢幕依序閃紅、綠、藍、白，然後出現中文字 → 螢幕 OK
  - 全白或全黑：檢查 TFT 腳位，或把 `TFT_IS_ILI9341` 改成 1
  - 顏色反相：`TFT_INVERT` 改成 1
- 喇叭發出 1 秒「嗶」→ 擴大機 OK
- 對麥克風說話，綠色音量條跳動 → 麥克風 OK（沒反應時可試 `MIC_USE_RIGHT 1`）
- 按按鍵出現橘色條 → 按鍵 OK

全部正常後，把 `HW_TEST_MODE` 改回 `0` 再上傳。

## 6. 使用方式

| 你說 | 機器人會 |
|---|---|
| 任何問題 | GPT 用繁體中文回答並唸出來 |
| 「拍照」「你看到什麼」「這是什麼」「幫我看上面寫什麼」 | 拍照後由 GPT 看圖回答（物件辨識、OCR）|
| 「大聲一點」「小聲一點」 | 直接調整音量，不用呼叫 AI |
| 「清除記憶」「重新開始」 | 忘掉先前的對話 |

也可以在 `PIN_POT` 填入旋鈕腳位，用擴展板上的旋鈕調音量（只能接 GPIO1–10）。

## 7. 費用參考（OpenAI，實際以官網為準）

每次對話會呼叫 3 次 API：語音轉文字（`gpt-4o-mini-transcribe`）、對話（`gpt-4o-mini`）、語音合成（`gpt-4o-mini-tts`）。一般問答每次約新台幣幾毛錢。可在 `config.h` 換模型。

## 已知限制

- 中文字型 `cjk_font.h` 由 `tools/make_font.py` 從 GNU Unifont 產生（16px，含 2 萬多個繁簡中文字與全形標點，約佔 740KB Flash）。
- HTTPS 使用 `setInsecure()`，沒有驗證憑證，方便但不嚴謹。正式產品建議加上 OpenAI 的根憑證。
- 目前只有「按鍵模式」，沒有做喚醒詞的聲控模式、SD 卡長期記憶（A/B 檔案摘要）和 Assistant API 知識庫。這些可以之後再加。

## 照片存到 Google 雲端硬碟

拍照後照片會顯示在螢幕上，並在背景上傳到雲端硬碟資料夾。第一次需要部署一個 Google Apps Script（只要做一次）：

1. 開啟 https://script.google.com → **新專案**
2. 把 `tools/GoogleDrive_Upload.gs` 的內容全部貼上，取代預設程式碼，按 **儲存**
3. 上方函式選單選 `testAccess` → 按 **執行** → 依畫面完成 Google 授權（會出現「Google 尚未驗證這個應用程式」，點「進階」→「前往（不安全）」，這是你自己的程式）
4. 右上角 **部署** → **新增部署作業** → 類型選 **網頁應用程式**
   - 執行身分：**我**
   - 誰可以存取：**所有人**
5. 按 **部署**，複製「網頁應用程式網址」（`https://script.google.com/macros/s/.../exec`）
6. 貼到 `config.h` 的 `GDRIVE_SCRIPT_URL`，重新上傳程式

檔名格式：`小柯_20260930_143015.jpg`。`GDRIVE_TOKEN` 是防止別人亂傳檔案的密碼，`.gs` 與 `config.h` 兩邊必須相同。

## 網頁設定（不用重新上傳程式）

Wi-Fi、密碼、OpenAI API Key、Google 雲端硬碟網址都可以用瀏覽器修改，存在板子的 NVS，重新上傳程式也不會消失。

- **平常**：機器人螢幕左下角顯示設定網址（例如 `http://192.168.6.167`），同一個 Wi-Fi 下用手機或電腦開啟即可；也可以用 `http://xiaoke.local`
- **連不上 Wi-Fi / 還沒設定**：機器人自動進入設定模式，開出熱點
  1. 手機連線 Wi-Fi `XiaoKe-Setup`，密碼 `xiaoke123`
  2. 開啟 `http://192.168.4.1`（多數手機會自動跳出設定頁）
  3. 從下拉選單選 Wi-Fi（或手動輸入）、填密碼與 API Key → 儲存，機器人會自動重新啟動
- 密碼與 API Key 欄位**留空 = 不變更**；網頁只顯示遮罩後的金鑰
- `config.h` 裡的 Wi-Fi / API Key 只當作第一次開機的預設值

## 按鍵：功能說明

聲控模式下（`VOICE_ACTIVATION 1`），按鍵用來瀏覽「功能說明」：

- 第一次按：開啟說明（聲控暫停，不會被雜音打斷）
- 每按一次：下一頁；到「二、創意開發者」頁時，小柯會親口介紹開發者
- 最後一頁再按：回到聊天（90 秒沒按也會自動回到聊天）

若改成 `VOICE_ACTIVATION 0`，按鍵恢復為「按住說話」。

## 創意開發者

此系統是**吳玉柱先生**與 Claude AI 共同開發，有任何意見請聯絡 achir1015@gmail.com。
問小柯「你是誰做的？」「你的開發者是誰？」，它也會介紹吳玉柱先生。

## 唱歌：YouTube 創作歌單

說「**唱首歌**」「來首歌」「放歌」「聽歌」，小柯會從 YouTube 播放清單隨機選一首（不會連續重複）：

- 螢幕顯示歌曲 **YouTube 封面**、歌名、詞曲創作者與 **QR 碼**
- 小柯會說「我來唱《歌名》給你聽！」並跟著節奏擺動、飄出音符
- **手機掃描 QR 碼**即可在 YouTube 播放完整歌曲（ESP32 無法直接播放 YouTube 影音）
- 唱歌畫面時暫停聲控（避免聽到手機音樂誤觸），按鍵或 3 分鐘後回到聊天

歌單設定：
- 預設播放清單：`YT_PLAYLIST_URL`（config.h），可在設定網頁修改
- 開機時自動在背景更新歌單（最多 80 首），也可在設定網頁按「更新歌單」或對小柯說「更新歌單」
- 歌單會存在板子裡，沒有網路時也能用上次的歌單選歌

歌曲版權屬於創作者吳玉柱（achir1015@gmail.com）。
