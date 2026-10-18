#include "../aipulse.c"
int main(void) {
    HDC dc=GetDC(NULL); int scales[]={96,120,144,192};
    const wchar_t* labels[]={L"15.9 / 15.9G",L"12.0 / 12.0G",L"unavailable"};
    for(int i=0;i<4;i++) {
        dpi=scales[i]; HFONT font=CreateFontW(-px(11),0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");
        HGDIOBJ old=SelectObject(dc,font);
        for(int j=0;j<3;j++) {
            SIZE size; GetTextExtentPoint32W(dc,labels[j],lstrlenW(labels[j]),&size);
            int available=px(VRAM_COLUMN-4)-px(3);
            printf("DPI %d, label %ls: text=%ldpx available=%dpx\n",dpi,labels[j],size.cx,available);
            if(size.cx>available) return 1;
        }
        SelectObject(dc,old); DeleteObject(font);
    }
    ReleaseDC(NULL,dc); return 0;
}
