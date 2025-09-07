#include "QUICServer.h"

/*
Google Style 코드 컨벤션
타입: PascalCase
함수: PascalCase
변수: snake_case
*/
int main() {
    QUICServer server;

    if(server.IsOk()) {
        server.ServerStart();
    }
}