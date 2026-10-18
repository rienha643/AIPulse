#include "../aipulse.c"
#define CHECK(condition,message) do { if(!(condition)) { fprintf(stderr,"FAIL: %s\n",message); return 1; } printf("PASS: %s\n",message); } while(0)
static HWND fixture(void) {
    return CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_TOPMOST|WS_EX_NOACTIVATE,L"STATIC",L"",WS_POPUP|WS_VISIBLE,-32000,-32000,32,16,NULL,NULL,GetModuleHandleW(NULL),NULL);
}
static int aboveOwner(HWND widget,HWND owner) {
    for(HWND h=GetWindow(owner,GW_HWNDPREV);h;h=GetWindow(h,GW_HWNDPREV)) if(h==widget) return 1;
    return 0;
}
int main(void) {
    InitializeCriticalSection(&guard);
    stopEvent=CreateEventW(NULL,TRUE,FALSE,NULL); wakeEvent=CreateEventW(NULL,FALSE,FALSE,NULL);
    WNDCLASSW cls={0}; cls.lpfnWndProc=proc; cls.hInstance=GetModuleHandleW(NULL); cls.lpszClassName=L"AIPulse.Native"; RegisterClassW(&cls);
    cls.lpszClassName=L"AIPulse.Controller"; RegisterClassW(&cls);
    controller=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,L"AIPulse.Controller",L"",WS_POPUP,0,0,0,0,NULL,NULL,cls.hInstance,NULL);
    HWND owner=fixture();
    CHECK(controller && owner && ensureDisplayOwner(owner),"owned display and independent controller created");
    SetWindowPos(window,HWND_TOPMOST,-32000,-32000,32,16,SWP_NOACTIVATE);
    ShowWindow(window,SW_SHOWNOACTIVATE);
    InterlockedExchange(&hidden,0);
    HWND original=window,foreground=GetForegroundWindow(); RECT before,after; GetWindowRect(window,&before);
    int stable=1;
    for(int i=0;i<100;i++) {
        SetWindowPos(owner,HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
        if(!IsWindowVisible(window)||!aboveOwner(window,owner)||window!=original) stable=0;
    }
    CHECK(stable,"100 owner raises never cover, hide, or recreate the display");
    CHECK(!restoreTaskbarStacking(),"owned display requires no polling-based stacking repair");
    CHECK(GetForegroundWindow()==foreground,"owner raises do not steal focus");
    GetWindowRect(window,&after); CHECK(EqualRect(&before,&after),"owner raises preserve display geometry");
    CHECK(!ensureDisplayOwner(owner) && window==original,"unchanged owner does not recreate the display");
    DestroyWindow(owner);
    CHECK(!window && IsWindow(controller),"owner destruction leaves the hidden controller running");
    CHECK(WaitForSingleObject(stopEvent,0)==WAIT_TIMEOUT,"owner destruction does not stop the app");
    owner=fixture(); CHECK(ensureDisplayOwner(owner),"display is recreated for a replacement owner");
    CHECK(GetWindow(window,GW_OWNER)==owner,"recreated display is owned by the replacement taskbar");
    HWND nextOwner=fixture();
    CHECK(ensureDisplayOwner(nextOwner) && GetWindow(window,GW_OWNER)==nextOwner,"owner replacement is handled even if old owner is still alive");
    DestroyWindow(owner);
    SendMessageW(controller,WM_CLOSE,0,0);
    CHECK(!IsWindow(controller) && !window && WaitForSingleObject(stopEvent,0)==WAIT_OBJECT_0,"explicit exit closes both windows and signals the worker");
    DestroyWindow(nextOwner); CloseHandle(stopEvent); CloseHandle(wakeEvent); DeleteCriticalSection(&guard);
    return 0;
}
