#include "platform/switch/switch_system.h"

#include <switch.h>

#include <cstdio>
#include <string>

namespace minicraft::platform::nx {

std::string DetectSystemLocaleTag() {
  if (R_FAILED(setInitialize())) {
    return "en-us";
  }

  u64 lang_code = 0;
  SetLanguage lang = SetLanguage_ENUS;
  if (R_SUCCEEDED(setGetSystemLanguage(&lang_code))) {
    setMakeLanguage(lang_code, &lang);
  }
  setExit();

  switch (lang) {
    case SetLanguage_JA:
      return "ja-jp";
    case SetLanguage_ENUS:
      return "en-us";
    case SetLanguage_FR:
    case SetLanguage_FRCA:
      return "fr-fr";
    case SetLanguage_DE:
      return "de-de";
    case SetLanguage_IT:
      return "it-it";
    case SetLanguage_ES:
      return "es-es";
    case SetLanguage_ES419:
      return "es-419";
    case SetLanguage_ZHCN:
    case SetLanguage_ZHHANS:
      return "zh-cn";
    case SetLanguage_ZHTW:
    case SetLanguage_ZHHANT:
      return "zh-tw";
    case SetLanguage_KO:
      return "ko-kr";
    case SetLanguage_NL:
      return "nl-nl";
    case SetLanguage_PT:
      return "pt-pt";
    case SetLanguage_PTBR:
      return "pt-br";
    case SetLanguage_RU:
      return "ru-ru";
    case SetLanguage_ENGB:
      return "en-gb";
    default:
      return "en-us";
  }
}

void ShowFatalErrorApplet(std::string_view dialog_message,
                          std::string_view details_message,
                          uint32_t error_number) {
  const std::string dialog_str(dialog_message);
  const std::string details_str(details_message);
  const char* details_ptr = details_str.empty() ? nullptr : details_str.c_str();

  u64 lang_code = 0;
  if (R_SUCCEEDED(setInitialize())) {
    setGetSystemLanguage(&lang_code);
    setExit();
  }

  const AppletType type = appletGetAppletType();
  if (type == AppletType_Application || type == AppletType_SystemApplication) {
    ErrorApplicationConfig cfg{};
    if (R_SUCCEEDED(errorApplicationCreate(&cfg, dialog_str.c_str(), details_ptr))) {
      errorApplicationSetNumber(&cfg, error_number);
      if (lang_code != 0) {
        errorApplicationSetLanguageCode(&cfg, lang_code);
      }
      errorApplicationShow(&cfg);
      return;
    }
  }

  ErrorSystemConfig sys_cfg{};
  if (R_SUCCEEDED(errorSystemCreate(&sys_cfg, dialog_str.c_str(), details_ptr))) {
    errorSystemSetCode(&sys_cfg, errorCodeCreate(2000, error_number));
    if (lang_code != 0) {
      errorSystemSetLanguageCode(&sys_cfg, lang_code);
    }
    errorSystemShow(&sys_cfg);
    return;
  }

  std::fprintf(stderr, "Fatal error (%u): %s\n%s\n", error_number,
               dialog_str.c_str(), details_str.c_str());
}

}  // namespace minicraft::platform::nx
