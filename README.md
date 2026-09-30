# 小柯：ESP32-S3 AI 語音視覺聊天機器人

一台放在桌上、**直接開口就能聊天**的 AI 機器人。它會聽、會說、會看，還有可愛的表情。
使用 Arduino IDE 開發，AI 服務採用 OpenAI（語音轉文字、GPT 對話、看圖、語音合成）。
<img width="807" height="495" alt="image" src="https://github.com/user-attachments/assets/ce08b40c-c6fb-45e6-956d-ed8253e8b30b" />
<img width="842" height="480" alt="image" src="https://github.com/user-attachments/assets/69f7a56b-9922-4ea7-91a4-96f2304c8f2c" />

> 創意開發者：**吳玉柱先生**與 Claude AI 共同開發
> 意見聯絡：achir1015@gmail.com

---

## 套件內容：Aduino 多功能 AI 語音影像辨識聊天機器人套件

**ESP32-S3 AI 語音視覺機器人**

| 項目 | 內容 | 數量 |
|---|---|---|
| 核心主板 | Goouuu 果云 ESP32-S3-CAM 開發板（內建 OV2640 鏡頭／WiFi／藍牙）| 1 |
| 專屬擴展板 | 整合 5V／3.3V 電源、DHT11 溫濕度計、旋鈕電位器 | 1 |
| 視覺螢幕 | 2.8 吋 ST7789-2 TFT 彩色螢幕（SPI 介面）| 1 |
| 數位聽覺 | INMP441 I2S 全向性數位麥克風 | 1 |
| 震撼音效 | MAX98357A I2S Class-D 數位放大器 | 1 |
| 發聲單體 | 高品質音樂專用腔體喇叭（含音箱，聲音飽滿）| 1 |
> 本專案實際使用的板子為 ESP32-S3 N16R8（16MB Flash + 8MB PSRAM），鏡頭為 **OV3660**，程式兩種鏡頭都支援。

---

## 功能

| 功能 | 說明 |
|---|---|
| 聲控聊天 | 直接說話，停頓約 0.9 秒自動送出，不用按鍵（語音活動偵測 + 觸發前 0.3 秒預錄）|
| AI 對話 | OpenAI `gpt-4o-mini-transcribe` 轉文字 → `gpt-4o-mini` 回答 → `gpt-4o-mini-tts` 串流播放，一律台灣繁體中文 |
| 拍照辨識 | 說「拍照」「這是什麼」「上面寫什麼」→ 拍照交給 GPT 看圖回答（物件、OCR）；照片全螢幕顯示並上傳 Google 雲端硬碟 |
| 可愛表情 | 藍色頭盔機器人，發光的眼睛、鼻子、嘴巴、酒窩、腮紅；會眨眼、東張西望；13 種表情（開心、愛心、驚訝、擔心、生氣、眨眼、害羞、無聊、想睡、聆聽、思考、暈眩…），**GPT 依回答內容選心情** |
| 時間列 | 日期、星期、**農曆**（2000–2098，逐日驗證）、時間、音量、Wi-Fi 燈號 |
| 語音指令 | 「大聲一點／小聲一點」「清除記憶」 |
| 網頁設定 | 瀏覽器開啟機器人 IP：Wi-Fi 下拉選單（掃描）、密碼、API Key、雲端硬碟網址；連不上 Wi-Fi 自動開熱點 `XiaoKe-Setup` |
| 按鍵說明 | 按鍵瀏覽「功能說明」，看完最後一頁回到聊天；開發者頁小柯會親口介紹 |
| 繁中字型 | 由 GNU Unifont 產生 16px 點陣字，涵蓋 2 萬多個中文字 |

## 實測腳位

| 元件 | 腳位 |
|---|---|
| 2.8" TFT（ST7789）| SCK=3、MOSI=45、CS=14、DC=47、RST=21 |
| INMP441 麥克風 | SCK=2、WS=1、SD=42（左聲道）|
| MAX98357A 擴大機 | BCLK=19、LRC=41、DIN=20 |
| 按鍵 | GPIO0（按下為 LOW）|
| 鏡頭 | 板載 DVP（XCLK=15、SIOD=4、SIOC=5…，見 `config.example.h`）|

套件沒有附接線資料，這些腳位是用 `PinFinder` 系列程式在板子上實測（麥克風聽擴大機的測試音、掃描 SPI 組合），再比對原廠韌體反組譯確認的。

## 專案結構

```
AI_Voice_Robot/            主程式（Arduino 草稿碼）
  AI_Voice_Robot.ino
  config.example.h         設定範本（複製成 config.h 填入自己的值；不上傳）
  web_page.h               設定網頁
  cjk_font.c / .h          繁中點陣字型（自動產生）
  lunar_table.h            農曆資料表（自動產生）
  README.md                詳細安裝與使用說明
tools/
  make_font.py             由 GNU Unifont 產生字型
  make_lunar.py            產生農曆資料表
  GoogleDrive_Upload.example.gs   照片上傳雲端硬碟的 Apps Script 範本
PinFinder ~ PinFinder7/    腳位偵測工具
```

## 快速開始

1. 安裝 Arduino IDE 2.x 與 **esp32 by Espressif 3.x**
2. 安裝函式庫：Adafruit GFX Library、Adafruit ST7735 and ST7789 Library、Adafruit ILI9341、ArduinoJson 7.x
3. 開發板選 **ESP32S3 Dev Module**，Flash Size **16MB**，Partition **16M Flash (3MB APP/9.9MB FATFS)**，PSRAM **OPI PSRAM**
4. 上傳 `AI_Voice_Robot`
5. 手機連 Wi-Fi `XiaoKe-Setup`（密碼 `xiaoke123`）→ 開啟 `http://192.168.4.1` → 選 Wi-Fi、輸入密碼與 OpenAI API Key → 儲存

詳細步驟、Google 雲端硬碟設定、參數調整請看 [AI_Voice_Robot/README.md](AI_Voice_Robot/README.md)。

## 授權與致謝

- 繁中字型資料來自 [GNU Unifont](https://unifoundry.com/unifont/)（GPL-2.0-or-later with font embedding exception / SIL OFL 1.1）
- 農曆資料由 Python [lunardate](https://pypi.org/project/lunardate/) 產生
- 此系統是**吳玉柱先生**與 **Claude AI** 共同開發，有任何意見請聯絡 achir1015@gmail.com
