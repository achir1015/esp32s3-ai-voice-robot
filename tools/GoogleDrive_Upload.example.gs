// 小柯機器人：照片上傳到 Google 雲端硬碟（範本：填入資料夾 ID 與 TOKEN，TOKEN 需與 config.h 的 GDRIVE_TOKEN 相同）
// 部署方式見 AI_Voice_Robot/README.md「照片存到 Google 雲端硬碟」
const FOLDER_ID = '你的雲端硬碟資料夾ID';   // 目標資料夾
const TOKEN = 'CHANGE_ME_TOKEN';                  // 必須和 config.h 的 GDRIVE_TOKEN 相同

// 機器人以 POST 傳送 base64 JPEG：?token=...&name=xxx.jpg
function doPost(e) {
  if (!e || !e.parameter || e.parameter.token !== TOKEN) {
    return ContentService.createTextOutput('forbidden');
  }
  const name = e.parameter.name || ('photo_' + new Date().getTime() + '.jpg');
  const bytes = Utilities.base64Decode(e.postData.contents);
  const blob = Utilities.newBlob(bytes, 'image/jpeg', name);
  const file = DriveApp.getFolderById(FOLDER_ID).createFile(blob);
  return ContentService.createTextOutput('ok ' + file.getId());
}

// 在編輯器按「執行」這個函式一次，完成授權並確認能寫入資料夾
function testAccess() {
  const folder = DriveApp.getFolderById(FOLDER_ID);
  Logger.log('資料夾名稱：' + folder.getName() + '（可以寫入）');
}
