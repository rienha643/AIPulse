#include "../aipulse.c"
#define CHECK(condition,message) do { if(!(condition)) { fprintf(stderr,"FAIL: %s\n",message); return 1; } printf("PASS: %s\n",message); } while(0)
static HWND fixture(void) {
    return CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_TOPMOST|WS_EX_NOACTIVATE,L"STATIC",L"",WS_POPUP|WS_VISIBLE,-32000,-32000,32,16,NULL,NULL,GetModuleHandleW(NULL),NULL);
}
int main(void) {
    HWND foreground=GetForegroundWindow();
    window=fixture(); taskbar=fixture();
    CHECK(window && taskbar,"offscreen fixture windows created");
    SetWindowPos(taskbar,HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
    RECT before,after; GetWindowRect(window,&before);
    CHECK(restoreTaskbarStacking(),"overlapping taskbar above widget is repaired");
    GetWindowRect(window,&after);
    CHECK(EqualRect(&before,&after),"position and size remain unchanged");
    CHECK(GetForegroundWindow()==foreground,"repair does not steal keyboard focus");
    CHECK(!restoreTaskbarStacking(),"already-correct stacking causes no repeated raise");
    customPosition=1;
    SetWindowPos(taskbar,HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
    CHECK(restoreTaskbarStacking(),"custom-position mode is also repaired");
    SetWindowPos(taskbar,HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
    InterlockedExchange(&hidden,1);
    CHECK(!restoreTaskbarStacking(),"intentional auto-hide is respected");
    InterlockedExchange(&hidden,0); ShowWindow(window,SW_HIDE);
    CHECK(!restoreTaskbarStacking(),"hidden widget is not shown by stacking repair");
    ShowWindow(window,SW_SHOWNOACTIVATE);
    SetWindowPos(taskbar,HWND_TOPMOST,-31900,-32000,0,0,SWP_NOSIZE|SWP_NOACTIVATE);
    CHECK(!restoreTaskbarStacking(),"non-overlapping windows are left alone");
    DestroyWindow(taskbar); DestroyWindow(window); return 0;
}
