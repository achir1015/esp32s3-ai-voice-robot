// ============================================================================
//  config.example.h — 設定範本（複製成 config.h 再填入自己的值；沒有 config.h 時自動使用本檔）
//  config.h — 所有可調整的設定都在這裡（Wi-Fi、API Key、腳位、模型、音量…）
//  ⚠ 此檔含 API Key，請勿上傳到 GitHub 或分享給他人
// ============================================================================
#pragma once

// ---------------------------------------------------------------- Wi-Fi ----
#define WIFI_SSID       "你的WiFi名稱"
#define WIFI_PASSWORD   "你的WiFi密碼"

// --------------------------------------------------------------- OpenAI ----
#define OPENAI_API_KEY  "sk-請填入你的OpenAI金鑰"  

#define STT_MODEL       "gpt-4o-mini-transcribe"   // 或 "whisper-1"
#define CHAT_MODEL      "gpt-4o-mini"              // 需支援影像輸入（拍照辨識用）
#define TTS_MODEL       "gpt-4o-mini-tts"          // 或 "tts-1"
#define TTS_VOICE       "nova"                     // alloy / ash / coral / echo / fable / nova / onyx / sage / shimmer
#define TTS_INSTRUCTIONS "用可愛、活潑、溫暖的台灣口音中文說話，語氣像貼心的小朋友，語速稍快。"

#define ROBOT_NAME      "小柯"
#define MAX_HISTORY_MSGS 10        // 記住最近幾則對話（user+assistant 各算 1 則）
#define CHAT_MAX_TOKENS  300

// ------------------------------------------------------------- 功能開關 ----
#define HW_TEST_MODE    0          // 1 = 開機只跑硬體測試（螢幕/喇叭/麥克風/鏡頭），先確認接線用
#define ENABLE_CAMERA   1

// ------------------------------------------------------- 照片上傳 Google 雲端硬碟 ----
// 部署 tools/GoogleDrive_Upload.gs 後，把「網頁應用程式網址」貼到這裡；留空 = 不上傳
#define GDRIVE_SCRIPT_URL ""
#define GDRIVE_TOKEN      "CHANGE_ME_TOKEN"   // 必須和 .gs 裡的 TOKEN 相同
#define PHOTO_SHOW_MS     3000     // 拍照後全螢幕顯示照片的時間

// ------------------------------------------------------------ 聲控（免按鍵）----
#define VOICE_ACTIVATION  1        // 1 = 直接說話就會開始聆聽；0 = 只用按鍵
#define VAD_MIN_RMS       250      // 觸發錄音的最低音量（環境吵、常誤觸就調高；喊很大聲才有反應就調低）
#define VAD_RATIO         3.0      // 音量需高於背景噪音幾倍才觸發
#define VAD_SILENCE_MS    900      // 停頓多久視為說完
#define VAD_MIN_SPEECH_MS 400      // 少於這個長度的聲音視為雜音
#define SLEEPY_AFTER_SEC  90       // 多久沒互動表情變想睡          // 1 = 啟用鏡頭，說「拍照」「你看到什麼」會把畫面送給 AI 看

// ================================================================ 腳位 ====
//  ESP32-S3-CAM 已被佔用、不可使用的腳位：
//    鏡頭 4,5,6,7,8,9,10,11,12,13,15,16,17,18   TF卡 38,39,40   PSRAM 35,36,37
//  可自由使用：0(BOOT鍵) 1 2 3 14 19 20 21 41 42 45 46 47 48(板載WS2812)
//  ✓ 以下腳位已由 PinFinder 實測 + 原廠韌體反組譯確認（2026-09-30）
// ---------------------------------------------------------------------------

// ---- 2.8 吋 SPI TFT（240x320）----
#define TFT_IS_ILI9341  0          // 原廠韌體使用 ST7789 驅動
#define TFT_SCK         3
#define TFT_MOSI        45
#define TFT_CS          14
#define TFT_DC          47
#define TFT_RST         21         // 沒接可設 -1
#define TFT_BL          -1         // 背光腳，模組直接接 3.3V 就設 -1
#define TFT_ROTATION    1          // 1 或 3 = 橫向 320x240；畫面顛倒改另一個
#define TFT_INVERT      0          // 此面板不需反相（設 1 會黑白顛倒）

// ---- INMP441 I2S 麥克風（L/R 腳接 GND = 左聲道）----
#define MIC_SCK         2
#define MIC_WS          1
#define MIC_SD          42
#define MIC_USE_RIGHT   0          // L/R 腳接 3.3V 時改 1
#define MIC_GAIN_SHIFT  14         // 錄音音量：數字越小越大聲（11~16），聲音太小/破音時調整

// ---- MAX98357A I2S 擴大機 ----
#define AMP_BCLK        19
#define AMP_LRC         41
#define AMP_DIN         20

// ---- 按鍵（HW-483 模組或板上 BOOT 鍵 = GPIO0）----
#define PIN_BUTTON      0
#define BUTTON_ACTIVE_LOW 1        // 按下為 LOW 設 1；按下為 HIGH 設 0

// ---- 擴展板旋鈕（音量）；只能用 GPIO1~10 (ADC1)，沒接設 -1 ----
#define PIN_POT         -1

// ---- 鏡頭（Goouuu ESP32-S3-CAM 固定腳位，不要改）----
#define CAM_PIN_PWDN    -1
#define CAM_PIN_RESET   -1
#define CAM_PIN_XCLK    15
#define CAM_PIN_SIOD    4
#define CAM_PIN_SIOC    5
#define CAM_PIN_D7      16
#define CAM_PIN_D6      17
#define CAM_PIN_D5      18
#define CAM_PIN_D4      12
#define CAM_PIN_D3      10
#define CAM_PIN_D2      8
#define CAM_PIN_D1      9
#define CAM_PIN_D0      11
#define CAM_PIN_VSYNC   6
#define CAM_PIN_HREF    7
#define CAM_PIN_PCLK    13

// ---------------------------------------------------------------- 音訊 ----
#define MIC_SAMPLE_RATE  16000
#define MAX_RECORD_SEC   12
#define TTS_SAMPLE_RATE  24000     // OpenAI TTS pcm 格式固定 24kHz / 16bit / mono
#define DEFAULT_VOLUME   70        // 0~100
