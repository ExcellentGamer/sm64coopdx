#include "djui.h"
#include "djui_panel_join_message.h"

bool gDjuiPanelJoinMessageVisible = false;
float gDownloadProgress = 0;
float gDownloadProgressInf = 0;
char gDownloadEstimate[DOWNLOAD_ESTIMATE_LENGTH] = "";

void djui_panel_join_message_error(UNUSED char* message) {
}

void djui_panel_join_message_create(UNUSED struct DjuiBase* caller) {
}

void djui_panel_do_host(UNUSED bool reconnecting, UNUSED bool playSound) {
}
