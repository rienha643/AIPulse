#define WIN32_LEAN_AND_MEAN
#ifndef UNICODE
#define UNICODE
#endif
#define _UNICODE
#include <windows.h>
#include <windowsx.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wtsapi32.h>

#define MAX_GPU 16
#define SAMPLE_MSG (WM_APP+1)
#define CATALOG_MSG (WM_APP+2)
#define VRAM_COLUMN 78
typedef void* Device;
typedef struct { unsigned int gpu, memory; } Util;
typedef struct { unsigned long long total, free, used; } Memory;
typedef int (*InitFn)(void);
typedef int (*CountFn)(unsigned int*);
typedef int (*HandleFn)(unsigned int,Device*);
typedef int (*NameFn)(Device,char*,unsigned int);
typedef int (*UuidHandleFn)(const char*,Device*);
typedef int (*UtilFn)(Device,Util*);
typedef int (*MemFn)(Device,Memory*);
typedef int (*ShutdownFn)(void);
typedef struct { Device dev; wchar_t name[96]; char uuid[96]; unsigned int load; Memory mem; int ok,loadOk,memoryOk; } GPU;
typedef struct { GPU cards[MAX_GPU]; unsigned int count; } Catalog;
static GPU cards[MAX_GPU];
static unsigned int count;
static HMODULE nvml;
static UtilFn getUtil; static MemFn getMemory; static ShutdownFn shutdownNvml;
static HWND window, taskbar, controller;
static LRESULT CALLBACK proc(HWND,UINT,WPARAM,LPARAM);
static void maintainDisplay(void);
static HANDLE stopEvent, wakeEvent, worker, singleton;
static CRITICAL_SECTION guard;
static volatile LONG interval=1000;
static volatile LONG hidden=0,screenOff=0,sessionLocked=0,suspended=0,forceReconnect=0;
static int nvmlReady=0;
static ULONGLONG retryAt=0;
static HPOWERNOTIFY powerNotification;
static UINT taskbarCreated;
static const GUID displayStateGuid={0x6fe69556,0x704a,0x47a0,{0x8f,0x24,0xc2,0x8d,0x93,0x6f,0xda,0x47}};
static unsigned long long utilQueries=0,memoryQueries=0,ramQueries=0,reconnects=0;
static HFONT normalFont, numberFont;
static int width=258,rowHeight=21,height=42,dpi=96;
/* GPU bits: 1=name, 2=load, 4=VRAM, 8=enabled. RAM: 1=load, 2=amount. */
static unsigned int gpuFields[MAX_GPU];
static int ramFields=3,gpuWidth=258,ramWidth=90;
static int nameColumn=103;
static MEMORYSTATUSEX ram={sizeof(MEMORYSTATUSEX)};
static int locked=1,customPosition=0,dragging=0,darkMode=1;
static POINT dragStart,windowStart;
static wchar_t ini[MAX_PATH];
static COLORREF bg,fg,muted;
static int px(int n) { return MulDiv(n,dpi,96); }
static unsigned long long displayedTenths(unsigned long long bytes) {
    return (bytes/1073741824ULL)*10+((bytes%1073741824ULL)*10+536870912ULL)/1073741824ULL;
}
static LONG atomicRead(volatile LONG* value) { return InterlockedCompareExchange(value,0,0); }
static int paused(void) { return atomicRead(&hidden)||atomicRead(&screenOff)||atomicRead(&sessionLocked)||atomicRead(&suspended); }
static void loadFields(void) {
    for(unsigned int i=0;i<count;i++) {
        wchar_t key[96],legacy[32]; wsprintfW(legacy,L"GPU%u",i);
        MultiByteToWideChar(CP_UTF8,0,cards[i].uuid,-1,key,96);
        int fallback=GetPrivateProfileIntW(L"Display",legacy,15,ini);
        gpuFields[i]=cards[i].uuid[0]?GetPrivateProfileIntW(L"GpuDisplay",key,fallback,ini)&15:fallback&15;
        if(cards[i].uuid[0]) { wchar_t value[16]; wsprintfW(value,L"%u",gpuFields[i]); WritePrivateProfileStringW(L"GpuDisplay",key,value,ini); }
    }
}
static void saveInventory(void) {
    wchar_t value[96],key[32]; wsprintfW(value,L"%u",count);
    WritePrivateProfileStringW(L"Inventory",L"Count",value,ini);
    for(unsigned int i=0;i<count;i++) {
        wsprintfW(key,L"Name%u",i); WritePrivateProfileStringW(L"Inventory",key,cards[i].name,ini);
        wsprintfW(key,L"Uuid%u",i); MultiByteToWideChar(CP_UTF8,0,cards[i].uuid,-1,value,96);
        WritePrivateProfileStringW(L"Inventory",key,value,ini);
    }
}
static void loadInventory(void) {
    count=GetPrivateProfileIntW(L"Inventory",L"Count",0,ini); if(count>MAX_GPU) count=MAX_GPU;
    for(unsigned int i=0;i<count;i++) {
        wchar_t key[32],value[96]; wsprintfW(key,L"Name%u",i);
        GetPrivateProfileStringW(L"Inventory",key,L"GPU",cards[i].name,96,ini);
        wsprintfW(key,L"Uuid%u",i); GetPrivateProfileStringW(L"Inventory",key,L"",value,96,ini);
        WideCharToMultiByte(CP_UTF8,0,value,-1,cards[i].uuid,96,NULL,NULL);
    }
}
static void saveSettings(void) {
    RECT r; wchar_t buf[32]; GetWindowRect(window,&r);
    wsprintfW(buf,L"%d",r.left); WritePrivateProfileStringW(L"Position",L"X",buf,ini);
    wsprintfW(buf,L"%d",r.top); WritePrivateProfileStringW(L"Position",L"Y",buf,ini);
    wsprintfW(buf,L"%d",customPosition); WritePrivateProfileStringW(L"Position",L"Custom",buf,ini);
    wsprintfW(buf,L"%d",locked); WritePrivateProfileStringW(L"Settings",L"Locked",buf,ini);
    wsprintfW(buf,L"%ld",interval); WritePrivateProfileStringW(L"Settings",L"Interval",buf,ini);
    wsprintfW(buf,L"%d",ramFields); WritePrivateProfileStringW(L"Display",L"RAM",buf,ini);
    for(unsigned int i=0;i<count;i++) {
        wchar_t key[32]; wsprintfW(key,L"GPU%u",i);
        wsprintfW(buf,L"%u",gpuFields[i]); WritePrivateProfileStringW(L"Display",key,buf,ini);
        if(cards[i].uuid[0]) { wchar_t uuid[96]; MultiByteToWideChar(CP_UTF8,0,cards[i].uuid,-1,uuid,96); WritePrivateProfileStringW(L"GpuDisplay",uuid,buf,ini); }
    }
}
static void disconnectNvml(void) {
    if(nvmlReady && shutdownNvml) shutdownNvml();
    nvmlReady=0;
    if(nvml) FreeLibrary(nvml);
    nvml=NULL; getUtil=NULL; getMemory=NULL; shutdownNvml=NULL;
    EnterCriticalSection(&guard);
    for(unsigned int i=0;i<count;i++) cards[i].dev=NULL;
    LeaveCriticalSection(&guard);
}
static int initialize(void) {
    InitFn init; CountFn getCount; HandleFn getHandle; NameFn getName;
    NameFn getUuid; UuidHandleFn byUuid;
    unsigned int discovered=0;
    reconnects++;
    nvml=LoadLibraryExW(L"nvml.dll",NULL,0x00000800); /* trusted System32 only */
    if(!nvml) return 0;
    init=(InitFn)GetProcAddress(nvml,"nvmlInit_v2");
    getCount=(CountFn)GetProcAddress(nvml,"nvmlDeviceGetCount_v2");
    getHandle=(HandleFn)GetProcAddress(nvml,"nvmlDeviceGetHandleByIndex_v2");
    getName=(NameFn)GetProcAddress(nvml,"nvmlDeviceGetName");
    getUuid=(NameFn)GetProcAddress(nvml,"nvmlDeviceGetUUID");
    byUuid=(UuidHandleFn)GetProcAddress(nvml,"nvmlDeviceGetHandleByUUID");
    getUtil=(UtilFn)GetProcAddress(nvml,"nvmlDeviceGetUtilizationRates");
    getMemory=(MemFn)GetProcAddress(nvml,"nvmlDeviceGetMemoryInfo");
    shutdownNvml=(ShutdownFn)GetProcAddress(nvml,"nvmlShutdown");
    if(!init||!getCount||!getHandle||!getName||!getUuid||!byUuid||!getUtil||!getMemory||!shutdownNvml||init()!=0) { disconnectNvml(); return 0; }
    nvmlReady=1;
    if(getCount(&discovered)!=0||!discovered) { disconnectNvml(); return 0; }
    EnterCriticalSection(&guard); unsigned int existingCount=count; LeaveCriticalSection(&guard);
    if(existingCount) {
        EnterCriticalSection(&guard);
        for(unsigned int i=0;i<count;i++) {
            cards[i].dev=NULL;
            if(cards[i].uuid[0]) byUuid(cards[i].uuid,&cards[i].dev);
            else getHandle(i,&cards[i].dev);
        }
        LeaveCriticalSection(&guard); return 1;
    }
    Catalog* catalog=(Catalog*)calloc(1,sizeof(Catalog)); if(!catalog) { disconnectNvml(); return 0; }
    if(discovered>MAX_GPU) discovered=MAX_GPU;
    for(unsigned int i=0;i<discovered;i++) {
        char name[96]={0};
        GPU* card=&catalog->cards[catalog->count];
        if(getHandle(i,&card->dev)!=0) continue;
        getName(card->dev,name,sizeof(name)); getUuid(card->dev,card->uuid,sizeof(card->uuid));
        const char* label=name;
        if(strncmp(label,"NVIDIA GeForce ",15)==0) label+=15;
        else if(strncmp(label,"NVIDIA ",7)==0) label+=7;
        MultiByteToWideChar(CP_UTF8,0,label,-1,card->name,96); catalog->count++;
    }
    if(!catalog->count) { free(catalog); disconnectNvml(); return 0; }
    if(controller) { if(!PostMessageW(controller,CATALOG_MSG,0,(LPARAM)catalog)) free(catalog); }
    else { count=catalog->count; memcpy(cards,catalog->cards,sizeof(cards)); free(catalog); }
    return 1;
}
static int ensureReady(void) {
    if(InterlockedExchange(&forceReconnect,0)) { disconnectNvml(); retryAt=0; }
    if(nvmlReady) return 1;
    if(GetTickCount64()<retryAt) return 0;
    retryAt=GetTickCount64()+5000;
    return initialize();
}
static int recoverable(int error) { return error && error!=3 && error!=4; }
static int sample(void) {
    unsigned int fields[MAX_GPU],n; int wantRam,wantGpu=0,changed=0,needsReconnect=0;
    EnterCriticalSection(&guard);
    n=count; memcpy(fields,gpuFields,sizeof(fields)); wantRam=ramFields;
    LeaveCriticalSection(&guard);
    for(unsigned int i=0;i<n;i++) if((fields[i]&8)&&(fields[i]&6)) wantGpu=1;
    MEMORYSTATUSEX current={sizeof(MEMORYSTATUSEX)};
    if(wantRam) {
        ramQueries++;
        if(GlobalMemoryStatusEx(&current)) {
            EnterCriticalSection(&guard);
            changed=ram.dwMemoryLoad!=current.dwMemoryLoad || ((wantRam&2)&&(displayedTenths(ram.ullTotalPhys-ram.ullAvailPhys)!=displayedTenths(current.ullTotalPhys-current.ullAvailPhys) || ram.ullTotalPhys!=current.ullTotalPhys));
            ram=current; LeaveCriticalSection(&guard);
        }
    }
    if(!wantGpu && n) { if(nvml) disconnectNvml(); return changed; }
    /* No saved inventory: discover hardware again when the driver becomes available. */
    int ready=(wantGpu||!n)?ensureReady():0;
    for(unsigned int i=0;i<n;i++) {
        if(!(fields[i]&8)||!(fields[i]&6)) continue;
        Util u={0}; Memory m={0};
        Device dev; EnterCriticalSection(&guard); dev=cards[i].dev; LeaveCriticalSection(&guard);
        int ue=1,me=1;
        if(ready && dev) {
            if(fields[i]&2) { utilQueries++; ue=getUtil(dev,&u); }
            if(fields[i]&4) { memoryQueries++; me=getMemory(dev,&m); }
        }
        if(ready && ((!dev)||((fields[i]&2)&&recoverable(ue))||((fields[i]&4)&&recoverable(me)))) needsReconnect=1;
        EnterCriticalSection(&guard);
        GPU* card=&cards[i];
        if(fields[i]&2) { if(card->loadOk!=(ue==0)||card->load!=u.gpu) changed=1; card->loadOk=ue==0; card->load=u.gpu; }
        if(fields[i]&4) { if(card->memoryOk!=(me==0)||displayedTenths(card->mem.used)!=displayedTenths(m.used)||card->mem.total!=m.total) changed=1; card->memoryOk=me==0; card->mem=m; }
        card->ok=card->loadOk&&card->memoryOk;
        LeaveCriticalSection(&guard);
    }
    if(needsReconnect) { disconnectNvml(); retryAt=GetTickCount64()+5000; }
    return changed;
}
static DWORD WINAPI samplingThread(void* unused) {
    (void)unused;
    HANDLE events[2]={stopEvent,wakeEvent};
    for(;;) {
        int sleeping=paused();
        if(!sleeping && sample()) PostMessageW(controller,SAMPLE_MSG,0,0);
        DWORD wait=WaitForMultipleObjects(2,events,FALSE,sleeping?INFINITE:(DWORD)atomicRead(&interval));
        if(wait!=WAIT_TIMEOUT && wait!=WAIT_OBJECT_0+1) break;
    }
    return 0;
}
static void updateTheme(void) {
    DWORD light=0,sz=sizeof(light);
    RegGetValueW(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",L"SystemUsesLightTheme",RRF_RT_REG_DWORD,NULL,&light,&sz);
    darkMode=!light;
    bg=darkMode?RGB(29,31,35):RGB(241,242,244);
    fg=darkMode?RGB(236,239,244):RGB(30,35,43);
    muted=darkMode?RGB(168,177,190):RGB(85,95,110);
}
static void anchor(void) {
    RECT bar,notify,screen; int x,y;
    taskbar=FindWindowW(L"Shell_TrayWnd",NULL);
    if(!taskbar || !GetWindowRect(taskbar,&bar)) return;
    GetWindowRect(GetDesktopWindow(),&screen);
    HWND tray=FindWindowExW(taskbar,NULL,L"TrayNotifyWnd",NULL);
    x=bar.right-width-px(250); y=bar.top+(bar.bottom-bar.top-height)/2;
    if(tray && GetWindowRect(tray,&notify) && notify.right>notify.left) x=notify.left-width-px(8);
    if(bar.bottom-bar.top>bar.right-bar.left) { x=bar.left; y=bar.bottom-height-px(180); }
    if(customPosition) {
        x=GetPrivateProfileIntW(L"Position",L"X",x,ini);
        y=GetPrivateProfileIntW(L"Position",L"Y",y,ini);
        RECT candidate={x,y,x+width,y+height};
        if(!MonitorFromRect(&candidate,MONITOR_DEFAULTTONULL)) { customPosition=0; anchor(); return; }
    }
    RECT current;
    if(!GetWindowRect(window,&current)||current.left!=x||current.top!=y||current.right-current.left!=width||current.bottom-current.top!=height)
        SetWindowPos(window,HWND_TOPMOST,x,y,width,height,SWP_NOACTIVATE);
}
static void layout(void) {
    int rows=0,maxWidth=0;
    /* Measure once when the selection changes; keep numeric columns aligned. */
    HDC dc=GetDC(NULL); HGDIOBJ oldFont=SelectObject(dc,normalFont);
    nameColumn=0;
    for(unsigned int i=0;i<count;i++) if((gpuFields[i]&9)==9) {
        SIZE size={0}; GetTextExtentPoint32W(dc,cards[i].name,lstrlenW(cards[i].name),&size);
        int measured=MulDiv(size.cx,96,dpi)+6;
        if(measured>nameColumn) nameColumn=measured;
    }
    if(nameColumn>103) nameColumn=103;
    SelectObject(dc,oldFont); ReleaseDC(NULL,dc);
    for(unsigned int i=0;i<count;i++) if(gpuFields[i]&8) {
        int n=20;
        if(gpuFields[i]&1) n+=nameColumn;
        if(gpuFields[i]&2) n+=34;
        if(gpuFields[i]&4) n+=VRAM_COLUMN;
        if(n>maxWidth) maxWidth=n;
        rows++;
    }
    gpuWidth=px(maxWidth);
    ramWidth=ramFields?px(100):0;
    width=gpuWidth+ramWidth;
    if(width<px(80)) width=px(80);
    if(rows<1) rows=1;
    if(ramFields && rows<2) rows=2;
    height=rowHeight*rows;
    if(window) { anchor(); InvalidateRect(window,NULL,FALSE); }
}
static int shouldHide(void) {
    RECT bar,r; HWND fore=GetForegroundWindow();
    if(!taskbar||!IsWindow(taskbar)) { anchor(); }
    if(!taskbar||!IsWindowVisible(taskbar)) return 1;
    if(GetWindowRect(taskbar,&bar) && bar.bottom-bar.top<=2) return 1;
    MONITORINFO barMonitor={sizeof(barMonitor)};
    if(GetMonitorInfoW(MonitorFromWindow(taskbar,MONITOR_DEFAULTTONEAREST),&barMonitor)) {
        RECT intersection;
        if(!IntersectRect(&intersection,&bar,&barMonitor.rcMonitor)||intersection.bottom-intersection.top<=2||intersection.right-intersection.left<=2) return 1;
    }
    if(fore && fore!=window && fore!=taskbar && GetWindowRect(fore,&r)) {
        wchar_t cls[64]; GetClassNameW(fore,cls,64);
        if(lstrcmpW(cls,L"Progman")==0||lstrcmpW(cls,L"WorkerW")==0) return 0;
        MONITORINFO mi={sizeof(mi)};
        if(GetMonitorInfoW(MonitorFromWindow(window,MONITOR_DEFAULTTONEAREST),&mi))
            if(r.left<=mi.rcMonitor.left && r.top<=mi.rcMonitor.top && r.right>=mi.rcMonitor.right && r.bottom>=mi.rcMonitor.bottom) return 1;
    }
    return 0;
}
static int restoreTaskbarStacking(void) {
    RECT widget,bar,overlap;
    if(paused()||!window||!taskbar||!IsWindowVisible(window)||!IsWindowVisible(taskbar)) return 0;
    if(!GetWindowRect(window,&widget)||!GetWindowRect(taskbar,&bar)||!IntersectRect(&overlap,&widget,&bar)) return 0;
    if(GetWindow(window,GW_OWNER)==taskbar) return 0; /* Windows keeps owned popups above their owner. */
    /* Explorer can raise its topmost taskbar without moving either window.
       Repair only that overlap; do not reposition or activate the widget. */
    HWND above=GetWindow(window,GW_HWNDPREV);
    for(int checked=0;above && checked<512;checked++,above=GetWindow(above,GW_HWNDPREV)) {
        if(above==taskbar) {
            int raised=SetWindowPos(window,HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
            if(raised) InvalidateRect(window,NULL,FALSE);
            return raised;
        }
    }
    return 0;
}
static void refreshVisibility(void) {
    int hide=atomicRead(&screenOff)||atomicRead(&sessionLocked)||atomicRead(&suspended)||shouldHide();
    if(InterlockedExchange(&hidden,hide)!=hide) {
        ShowWindow(window,hide?SW_HIDE:SW_SHOWNOACTIVATE);
        if(!hide) InvalidateRect(window,NULL,FALSE);
        SetEvent(wakeEvent);
    }
    if(!hide) restoreTaskbarStacking();
}
static void powerStateChanged(void) {
    if(atomicRead(&screenOff)||atomicRead(&sessionLocked)||atomicRead(&suspended)) KillTimer(controller,1);
    else SetTimer(controller,1,1000,NULL);
    refreshVisibility(); SetEvent(wakeEvent);
}
static HWND createOwnedDisplay(HWND owner) {
    return CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_TOPMOST|WS_EX_NOACTIVATE,L"AIPulse.Native",L"AI Pulse",WS_POPUP,0,0,width,height,owner,NULL,GetModuleHandleW(NULL),NULL);
}
static int ensureDisplayOwner(HWND owner) {
    if(window && IsWindow(window) && GetWindow(window,GW_OWNER)!=owner) DestroyWindow(window);
    taskbar=owner;
    if(!taskbar) { InterlockedExchange(&hidden,1); SetEvent(wakeEvent); return 0; }
    if(window && IsWindow(window)) return 0;
    window=createOwnedDisplay(taskbar);
    if(!window) { InterlockedExchange(&hidden,1); SetEvent(wakeEvent); return 0; }
    return 1;
}
static void maintainDisplay(void) {
    int created=ensureDisplayOwner(FindWindowW(L"Shell_TrayWnd",NULL));
    if(!window) return;
    if(created) {
        anchor();
        if(!paused()) ShowWindow(window,SW_SHOWNOACTIVATE);
        InvalidateRect(window,NULL,FALSE);
    }
    if(!customPosition&&!dragging) anchor();
    refreshVisibility();
}
static void textAt(HDC dc,const wchar_t* s,int left,int top,int right,int bottom,UINT align,HFONT font,COLORREF color) {
    RECT r={left,top,right,bottom}; SelectObject(dc,font); SetTextColor(dc,color);
    DrawTextW(dc,s,-1,&r,DT_SINGLELINE|DT_VCENTER|DT_NOPREFIX|align);
}
static void paint(void) {
    PAINTSTRUCT ps; HDC dc=BeginPaint(window,&ps); RECT bounds={0,0,width,height};
    HDC mem=CreateCompatibleDC(dc); HBITMAP bitmap=CreateCompatibleBitmap(dc,width,height);
    HGDIOBJ old=SelectObject(mem,bitmap); HBRUSH brush=CreateSolidBrush(bg);
    FillRect(mem,&bounds,brush); DeleteObject(brush); SetBkMode(mem,TRANSPARENT);
    GPU snapshot[MAX_GPU]; MEMORYSTATUSEX memory;
    EnterCriticalSection(&guard); memcpy(snapshot,cards,sizeof(cards)); memory=ram; LeaveCriticalSection(&guard);
    int row=0;
    for(unsigned int i=0;i<count;i++) {
        if(!(gpuFields[i]&8)) continue;
        int y=row++*rowHeight,x=12; wchar_t load[24],vram[64];
        COLORREF accent=i%2?RGB(115,174,255):RGB(60,203,164);
        if(!darkMode) accent=i%2?RGB(43,102,185):RGB(0,123,95);
        RECT mark={px(4),y+px(5),px(6),y+rowHeight-px(5)};
        brush=CreateSolidBrush(accent); FillRect(mem,&mark,brush); DeleteObject(brush);
        if(snapshot[i].loadOk) swprintf(load,24,L"%u%%",snapshot[i].load); else lstrcpyW(load,L"--");
        if(snapshot[i].memoryOk) swprintf(vram,64,L"%.1f / %.1fG",snapshot[i].mem.used/1073741824.0,snapshot[i].mem.total/1073741824.0);
        else lstrcpyW(vram,L"unavailable");
        if(gpuFields[i]&1) {
            textAt(mem,snapshot[i].name,px(x),y,px(x+nameColumn),y+rowHeight,DT_LEFT|DT_END_ELLIPSIS,normalFont,fg); x+=nameColumn;
        }
        if(gpuFields[i]&2) {
            textAt(mem,load,px(x+1),y,px(x+33),y+rowHeight,DT_LEFT,numberFont,accent); x+=34;
        }
        if(gpuFields[i]&4) {
            textAt(mem,vram,px(x+3),y,px(x+VRAM_COLUMN-4),y+rowHeight,DT_RIGHT,numberFont,muted);
            if(snapshot[i].memoryOk && snapshot[i].mem.total) {
                RECT line={px(x+3),y+rowHeight-px(2),px(x+3)+(int)(px(VRAM_COLUMN-7)*(double)snapshot[i].mem.used/snapshot[i].mem.total),y+rowHeight-px(1)};
                brush=CreateSolidBrush(accent); FillRect(mem,&line,brush); DeleteObject(brush);
            }
        }
    }
    if(ramFields) {
        wchar_t label[32],amount[64]; COLORREF accent=darkMode?RGB(205,172,245):RGB(132,79,174);
        RECT divider={gpuWidth,px(6),gpuWidth+px(1),height-px(6)};
        brush=CreateSolidBrush(darkMode?RGB(65,68,75):RGB(210,213,220));
        if(gpuWidth) FillRect(mem,&divider,brush); DeleteObject(brush);
        if(ramFields&1) swprintf(label,32,L"RAM  %lu%%",memory.dwMemoryLoad); else lstrcpyW(label,L"RAM");
        swprintf(amount,64,L"%.1f / %.1fG",(memory.ullTotalPhys-memory.ullAvailPhys)/1073741824.0,memory.ullTotalPhys/1073741824.0);
        int center=(height-rowHeight*((ramFields&2)?2:1))/2;
        textAt(mem,label,gpuWidth+px(7),center,width-px(6),center+rowHeight,DT_CENTER,normalFont,accent);
        if(ramFields&2) textAt(mem,amount,gpuWidth+px(7),center+rowHeight,width-px(6),center+2*rowHeight,DT_CENTER,numberFont,muted);
        RECT line={gpuWidth+px(8),height-px(2),gpuWidth+px(8)+(int)((ramWidth-px(16))*memory.dwMemoryLoad/100),height-px(1)};
        brush=CreateSolidBrush(accent); FillRect(mem,&line,brush); DeleteObject(brush);
    }
    if(!row && !ramFields) textAt(mem,L"우클릭: 설정",px(4),0,width-px(4),height,DT_CENTER,normalFont,muted);
    BitBlt(dc,0,0,width,height,mem,0,0,SRCCOPY);
    SelectObject(mem,old); DeleteObject(bitmap); DeleteDC(mem); EndPaint(window,&ps);
}
static int startupEnabled(void) {
    wchar_t value[MAX_PATH+4]; DWORD sz=sizeof(value);
    return RegGetValueW(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",L"AIPulse",RRF_RT_REG_SZ,NULL,value,&sz)==ERROR_SUCCESS;
}
static void toggleStartup(void) {
    HKEY key; wchar_t path[MAX_PATH],quoted[MAX_PATH+4]; int enabled=startupEnabled();
    if(RegCreateKeyExW(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",0,NULL,0,KEY_SET_VALUE,NULL,&key,NULL)!=ERROR_SUCCESS) return;
    if(enabled) RegDeleteValueW(key,L"AIPulse");
    else { GetModuleFileNameW(NULL,path,MAX_PATH); swprintf(quoted,MAX_PATH+4,L"\"%ls\"",path); RegSetValueExW(key,L"AIPulse",0,REG_SZ,(BYTE*)quoted,(lstrlenW(quoted)+1)*sizeof(wchar_t)); }
    RegCloseKey(key);
}
static void menu(void) {
    HMENU m=CreatePopupMenu(); POINT p; GetCursorPos(&p);
    AppendMenuW(m,MF_STRING|MF_DISABLED,0,L"AI Pulse");
    AppendMenuW(m,MF_SEPARATOR,0,NULL);
    for(unsigned int i=0;i<count;i++) {
        HMENU sub=CreatePopupMenu(); unsigned int bits[4]={8,1,2,4};
        const wchar_t* labels[4]={L"이 GPU 표시",L"이름",L"부하 (%)",L"VRAM 사용량 / 전체"};
        for(int j=0;j<4;j++) AppendMenuW(sub,MF_STRING|((gpuFields[i]&bits[j])?MF_CHECKED:0),100+i*4+j,labels[j]);
        AppendMenuW(m,MF_POPUP,(UINT_PTR)sub,cards[i].name);
    }
    HMENU ramMenu=CreatePopupMenu();
    AppendMenuW(ramMenu,MF_STRING|((ramFields&1)?MF_CHECKED:0),200,L"RAM 사용률 (%)");
    AppendMenuW(ramMenu,MF_STRING|((ramFields&2)?MF_CHECKED:0),201,L"RAM 사용량 / 전체");
    AppendMenuW(m,MF_POPUP,(UINT_PTR)ramMenu,L"RAM 표시 항목");
    AppendMenuW(m,MF_STRING,202,L"기본 표시로 복원");
    AppendMenuW(m,MF_SEPARATOR,0,NULL);
    AppendMenuW(m,MF_STRING| (locked?MF_CHECKED:0),1,L"위치 고정 (해제하면 드래그)");
    AppendMenuW(m,MF_STRING,2,L"시계 왼쪽으로 위치 초기화");
    AppendMenuW(m,MF_SEPARATOR,0,NULL);
    AppendMenuW(m,MF_STRING|(interval==1000?MF_CHECKED:0),3,L"갱신: 1초");
    AppendMenuW(m,MF_STRING|(interval==2000?MF_CHECKED:0),4,L"갱신: 2초");
    AppendMenuW(m,MF_STRING|(interval==5000?MF_CHECKED:0),5,L"갱신: 5초");
    AppendMenuW(m,MF_SEPARATOR,0,NULL);
    AppendMenuW(m,MF_STRING|(startupEnabled()?MF_CHECKED:0),6,L"Windows 시작 시 실행");
    AppendMenuW(m,MF_STRING,7,L"종료");
    SetForegroundWindow(window);
    int result=TrackPopupMenu(m,TPM_RETURNCMD|TPM_NONOTIFY|TPM_RIGHTBUTTON,p.x,p.y,0,window,NULL);
    DestroyMenu(m); PostMessageW(window,WM_NULL,0,0);
    /* The sampling thread reads the selection under the same lock. */
    EnterCriticalSection(&guard);
    if(result>=100 && result<100+(int)count*4) {
        int i=(result-100)/4,j=(result-100)%4; unsigned int bits[4]={8,1,2,4};
        gpuFields[i]^=bits[j];
        if(j==0 && (gpuFields[i]&8) && !(gpuFields[i]&7)) gpuFields[i]=15;
        if(!(gpuFields[i]&7)) gpuFields[i]&=~8u;
        if(j && (gpuFields[i]&7)) gpuFields[i]|=8;
    }
    if(result==200 || result==201) ramFields^=result==200?1:2;
    if(result==202) { for(unsigned int i=0;i<count;i++) gpuFields[i]=15; ramFields=3; }
    LeaveCriticalSection(&guard);
    if(result>=100) { layout(); SetEvent(wakeEvent); }
    if(result==1) locked=!locked;
    if(result==2) { customPosition=0; anchor(); }
    if(result>=3 && result<=5) { InterlockedExchange(&interval,result==3?1000:result==4?2000:5000); SetEvent(wakeEvent); }
    if(result==6) toggleStartup();
    if(result==7) PostMessageW(controller,WM_CLOSE,0,0); else if(result) saveSettings();
}
static LRESULT CALLBACK proc(HWND w,UINT msg,WPARAM wp,LPARAM lp) {
    if(w==controller && taskbarCreated && msg==taskbarCreated) { taskbar=NULL; maintainDisplay(); return 0; }
    switch(msg) {
    case WM_PAINT: if(w==window) { paint(); return 0; } break;
    case WM_ERASEBKGND: return 1;
    case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
    case SAMPLE_MSG: if(window) InvalidateRect(window,NULL,FALSE); return 0;
    case CATALOG_MSG: {
        Catalog* catalog=(Catalog*)lp;
        EnterCriticalSection(&guard);
        if(!count) { count=catalog->count; memcpy(cards,catalog->cards,sizeof(cards)); loadFields(); }
        LeaveCriticalSection(&guard);
        free(catalog); saveInventory(); layout(); SetEvent(wakeEvent); return 0;
    }
    case WM_TIMER:
        maintainDisplay(); return 0;
    case WM_WTSSESSION_CHANGE:
        if(wp==WTS_SESSION_LOCK) InterlockedExchange(&sessionLocked,1);
        if(wp==WTS_SESSION_UNLOCK) InterlockedExchange(&sessionLocked,0);
        powerStateChanged(); return 0;
    case WM_POWERBROADCAST:
        if(wp==PBT_POWERSETTINGCHANGE) {
            POWERBROADCAST_SETTING* setting=(POWERBROADCAST_SETTING*)lp;
            if(memcmp(&setting->PowerSetting,&displayStateGuid,sizeof(GUID))==0 && setting->DataLength>=sizeof(DWORD)) {
                DWORD state; memcpy(&state,setting->Data,sizeof(state)); InterlockedExchange(&screenOff,state==0); powerStateChanged();
            }
        }
        if(wp==PBT_APMSUSPEND) { InterlockedExchange(&suspended,1); powerStateChanged(); }
        if(wp==PBT_APMRESUMEAUTOMATIC||wp==PBT_APMRESUMESUSPEND) {
            InterlockedExchange(&suspended,0); InterlockedExchange(&forceReconnect,1); powerStateChanged();
        }
        return TRUE;
    case WM_SETTINGCHANGE: case WM_THEMECHANGED: updateTheme(); if(window) InvalidateRect(window,NULL,FALSE); return 0;
    case WM_DISPLAYCHANGE: maintainDisplay(); return 0;
    case WM_CONTEXTMENU: menu(); return 0;
    case WM_LBUTTONDOWN:
        if(!locked) { RECT r; dragging=1; GetCursorPos(&dragStart); GetWindowRect(w,&r); windowStart.x=r.left; windowStart.y=r.top; SetCapture(w); } return 0;
    case WM_MOUSEMOVE:
        if(dragging) { POINT p; GetCursorPos(&p); SetWindowPos(w,HWND_TOPMOST,windowStart.x+p.x-dragStart.x,windowStart.y+p.y-dragStart.y,0,0,SWP_NOSIZE|SWP_NOACTIVATE); } return 0;
    case WM_LBUTTONUP:
        if(dragging) { dragging=0; ReleaseCapture(); customPosition=1; saveSettings(); } return 0;
    case WM_CAPTURECHANGED: dragging=0; return 0;
    case WM_CLOSE:
        if(window && IsWindow(window)) DestroyWindow(window);
        if(controller && IsWindow(controller)) DestroyWindow(controller);
        return 0;
    case WM_DESTROY:
        if(w==window) {
            window=NULL; dragging=0; InterlockedExchange(&hidden,1); SetEvent(wakeEvent);
        }
        if(w==controller) {
            KillTimer(w,1); WTSUnRegisterSessionNotification(w);
            if(powerNotification) UnregisterPowerSettingNotification(powerNotification);
            SetEvent(stopEvent); PostQuitMessage(0);
        }
        return 0;
    }
    return DefWindowProcW(w,msg,wp,lp);
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE prev,LPWSTR args,int show) {
    (void)prev; (void)show;
    /* Stable AI Pulse identity for single-instance control. */
    if(wcsstr(args,L"--quit")) {
        HWND existing=FindWindowW(L"AIPulse.Controller",NULL);
        if(!existing) existing=FindWindowW(L"AIPulse.Native",NULL);
        if(existing) PostMessageW(existing,WM_CLOSE,0,0); return 0;
    }
    singleton=CreateMutexW(NULL,TRUE,L"Local\\AIPulse.Native.v1");
    if(!singleton||GetLastError()==ERROR_ALREADY_EXISTS) return 0;
    SetProcessDPIAware(); InitializeCriticalSection(&guard);
    GetModuleFileNameW(NULL,ini,MAX_PATH); wchar_t* slash=wcsrchr(ini,L'\\'); if(slash) lstrcpyW(slash+1,L"AIPulse.ini");
    loadInventory();
    if(initialize()) saveInventory();
    ramFields=GetPrivateProfileIntW(L"Display",L"RAM",3,ini)&3; loadFields();
    if(wcsstr(args,L"--snapshot")) {
        sample(); wchar_t out[MAX_PATH]; lstrcpyW(out,ini); wchar_t* s=wcsrchr(out,L'\\'); if(s) lstrcpyW(s+1,L"snapshot.txt");
        FILE* f=_wfopen(out,L"w"); if(f) { for(unsigned int i=0;i<count;i++) fprintf(f,"GPU %u: %ls | %u%% | %llu / %llu bytes | valid=%d\n",i,cards[i].name,cards[i].load,cards[i].mem.used,cards[i].mem.total,cards[i].ok); fprintf(f,"RAM: %lu%% | %llu / %llu bytes\n",ram.dwMemoryLoad,ram.ullTotalPhys-ram.ullAvailPhys,ram.ullTotalPhys); fclose(f); }
        disconnectNvml(); return 0;
    }
    HDC dc=GetDC(NULL); dpi=GetDeviceCaps(dc,LOGPIXELSX); ReleaseDC(NULL,dc);
    width=px(258); rowHeight=px(21); height=rowHeight*count;
    normalFont=CreateFontW(-px(11),0,0,0,FW_MEDIUM,0,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");
    numberFont=CreateFontW(-px(11),0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");
    locked=GetPrivateProfileIntW(L"Settings",L"Locked",1,ini);
    customPosition=GetPrivateProfileIntW(L"Position",L"Custom",0,ini);
    interval=GetPrivateProfileIntW(L"Settings",L"Interval",1000,ini);
    if(interval!=1000 && interval!=2000 && interval!=5000) interval=1000;
    layout();
    updateTheme(); stopEvent=CreateEventW(NULL,TRUE,FALSE,NULL); wakeEvent=CreateEventW(NULL,FALSE,FALSE,NULL);
    WNDCLASSW cls={0}; cls.lpfnWndProc=proc; cls.hInstance=instance; cls.lpszClassName=L"AIPulse.Native"; cls.hCursor=LoadCursor(NULL,IDC_ARROW); RegisterClassW(&cls);
    /* Explorer owns the display to maintain its stacking atomically. The hidden
       controller survives owner destruction and recreates the display. */
    cls.lpszClassName=L"AIPulse.Controller"; RegisterClassW(&cls);
    controller=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,L"AIPulse.Controller",L"AI Pulse Controller",WS_POPUP,0,0,0,0,NULL,NULL,instance,NULL);
    if(!controller) return 2;
    taskbarCreated=RegisterWindowMessageW(L"TaskbarCreated");
    WTSRegisterSessionNotification(controller,NOTIFY_FOR_THIS_SESSION);
    powerNotification=RegisterPowerSettingNotification(controller,&displayStateGuid,DEVICE_NOTIFY_WINDOW_HANDLE);
    SetTimer(controller,1,1000,NULL); maintainDisplay();
    worker=CreateThread(NULL,0,samplingThread,NULL,0,NULL);
    MSG msg; while(GetMessageW(&msg,NULL,0,0)>0) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    SetEvent(stopEvent); if(worker) { WaitForSingleObject(worker,INFINITE); CloseHandle(worker); }
    CloseHandle(stopEvent); CloseHandle(wakeEvent); DeleteObject(normalFont); DeleteObject(numberFont); disconnectNvml(); DeleteCriticalSection(&guard); CloseHandle(singleton);
    return 0;
}
