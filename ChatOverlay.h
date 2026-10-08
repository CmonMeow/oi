#pragma once

// A small premultiplied-alpha surface for the existing chat drawing commands.
// OpenGL windows use CS_OWNDC and cannot themselves be layered windows.
class ChatOverlayCanvas {
    HDC dc=nullptr;
    HBITMAP bitmap=nullptr;
    HGDIOBJ previous=nullptr;
    unsigned char* pixels=nullptr;
    int width=0,height=0,surfaceWidth=0,surfaceHeight=0;
    void release() {
        if(previous)SelectObject(dc,previous);
        if(bitmap)DeleteObject(bitmap);
        if(dc)DeleteDC(dc);
        dc=nullptr;bitmap=nullptr;previous=nullptr;pixels=nullptr;width=height=surfaceWidth=surfaceHeight=0;
    }
    void pixel(int x,int y,float r,float g,float b,float alpha) {
        if(x<0||x>=width||y<0||y>=height)return;
        const float keep=1.f-alpha;
        for(int row=(height-1-y)*surfaceHeight/height;row<(height-y)*surfaceHeight/height;++row)
            for(int col=x*surfaceWidth/width;col<(x+1)*surfaceWidth/width;++col) {
                auto p=pixels+4*(row*surfaceWidth+col);
                p[0]=(unsigned char)(b*255.f*alpha+p[0]*keep+.5f);
                p[1]=(unsigned char)(g*255.f*alpha+p[1]*keep+.5f);
                p[2]=(unsigned char)(r*255.f*alpha+p[2]*keep+.5f);
                p[3]=(unsigned char)(255.f*alpha+p[3]*keep+.5f);
            }
    }
public:
    bool active=false;
    void clear(){active=false;release();}
    ~ChatOverlayCanvas(){release();}
    bool begin(int w,int h,unsigned dpi=96) {
        active=false;
        if(w<=0||h<=0||w>8192||h>8192)return false;
        const int pw=MulDiv(w,dpi,96),ph=MulDiv(h,dpi,96);
        if(pw<=0||ph<=0||pw>8192||ph>8192)return false;
        if(w!=width||h!=height||pw!=surfaceWidth||ph!=surfaceHeight) {
            release();dc=CreateCompatibleDC(nullptr);
            if(!dc)return false;
            BITMAPINFO info={};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
            info.bmiHeader.biWidth=pw;info.bmiHeader.biHeight=-ph;info.bmiHeader.biPlanes=1;
            info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
            bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,(void**)&pixels,nullptr,0);
            if(!bitmap){release();return false;}
            previous=SelectObject(dc,bitmap);width=w;height=h;surfaceWidth=pw;surfaceHeight=ph;
        }
        // 50% dark-grey backdrop; glyphs and controls retain their own opacity.
        for(int i=0;i<surfaceWidth*surfaceHeight;++i){pixels[i*4]=pixels[i*4+1]=pixels[i*4+2]=13;pixels[i*4+3]=128;}
        active=true;return true;
    }
    void rect(float x1,float y1,float x2,float y2,float r,float g,float b,float a,bool outline) {
        int left=(std::max)(0,(int)x1),right=(std::min)(width,(int)x2);
        int bottom=(std::max)(0,(int)y1),top=(std::min)(height,(int)y2);
        for(int y=bottom;y<top;++y)for(int x=left;x<right;++x)
            if(!outline||x==left||x==right-1||y==bottom||y==top-1)pixel(x,y,r,g,b,a);
    }
    void text(const char* text,float x,float y,float r,float g,float b) {
        x=(std::max)(0.f,x);y=(std::max)(2.f,y);
        for(int pass=0;pass<2;++pass) {
            int px=(int)x+(pass==0),py=(int)y-(pass==0);
            for(auto c=(const unsigned char*)text;*c;++c) {
                if(*c=='\n'){px=(int)x+(pass==0);py-=16;continue;}
                int advance=*c==' '?6:*c=='\t'?18:*c>=33&&*c<=127?12:0;
                if(*c>=33&&*c<=127)for(int row=0;row<16;++row)for(int col=0;col<16;++col)
                    if(serif[*c-32][row*2+col/8]&(0x80>>(col%8)))
                        pixel(px+col,py+row,pass?r:.065f,pass?g:.07f,pass?b:.07f,1.f);
                px+=advance;
            }
        }
    }
    bool present(HWND window) {
        active=false;
        const auto previousDpi=SetThreadDpiAwarenessContext(GetWindowDpiAwarenessContext(window));
        POINT origin={};SIZE size={surfaceWidth,surfaceHeight};BLENDFUNCTION blend={AC_SRC_OVER,0,255,AC_SRC_ALPHA};
        const bool result=UpdateLayeredWindow(window,nullptr,nullptr,&size,dc,&origin,0,&blend,ULW_ALPHA)!=FALSE;
        SetWindowPos(window,HWND_TOP,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE|SWP_SHOWWINDOW);
        if(previousDpi)SetThreadDpiAwarenessContext(previousDpi);
        return result;
    }
};
