#include "input_hooker.h"
//#include "macro.h"
#include "detours\detours.h"
#include "detours\detours_ext.h"
#include "user32_hooker.h"
#include "render_context.h"
#include "utils.h"

namespace maku
{
namespace render
{
static bool call_out = false;
static bool windowed = true;
static Rect game_rect; //game render rect
static Rect back_rect; //back buffer rect
static HHOOK hhook = 0;
static HWND cur_hwnd = NULL;
static DWORD last_tick = 0;
static const DWORD hotkey = 0x1009;
//�����Ƿ���
static bool hotkey_pressed = false;

static POINT cursor_pos = {0, 0};
static POINT org_cursor_pos = {100, 100};

typedef BOOL (WINAPI *PeekMessageA_T)(LPMSG lpMsg,HWND hWnd,UINT wMsgFilterMin,UINT wMsgFilterMax,UINT wRemoveMsg);
typedef BOOL (WINAPI *PeekMessageW_T)(LPMSG lpMsg,HWND hWnd,UINT wMsgFilterMin,UINT wMsgFilterMax,UINT wRemoveMsg);
typedef BOOL (WINAPI *GetMessageA_T)(LPMSG lpMsg,HWND hWnd,UINT wMsgFilterMin,UINT wMsgFilterMax);
typedef BOOL (WINAPI *GetMessageW_T)(LPMSG lpMsg,HWND hWnd,UINT wMsgFilterMin,UINT wMsgFilterMax);

PeekMessageA_T TPeekMessageA = 0;
PeekMessageW_T TPeekMessageW = 0;
GetMessageA_T TGetMessageA = 0;
GetMessageW_T TGetMessageW = 0;

BOOL WINAPI FPeekMessageA(LPMSG lpMsg,HWND hWnd,UINT wMsgFilterMin,UINT wMsgFilterMax,UINT wRemoveMsg);
BOOL WINAPI FPeekMessageW(LPMSG lpMsg,HWND hWnd,UINT wMsgFilterMin,UINT wMsgFilterMax,UINT wRemoveMsg);
BOOL WINAPI FGetMessageA(LPMSG lpMsg,HWND hWnd,UINT wMsgFilterMin,UINT wMsgFilterMax);
BOOL WINAPI FGetMessageW(LPMSG lpMsg,HWND hWnd,UINT wMsgFilterMin,UINT wMsgFilterMax);

static bool InstallHook(HWND hwnd);

static void UninstallHook();

static HWND GetGameWindow(DWORD & thread_id);

static void TransPoint(int old_x, int old_y, int & new_x, int & new_y);

static void HandleMouseMessage(MSG & msg);

LRESULT CALLBACK GetMsgProc(
    _In_  int code,
    _In_  WPARAM wParam,
    _In_  LPARAM lParam
    );
//////////////////////////////////////////////////////////////////////////
void InputHooker::CallOut(bool b)
{
    if (b != call_out)
    {
        int p = b ? 1 : 0;
        PostMessage(cur_hwnd, WM_CUSTOM_MESSAGE, 
            WPARAM_SHIELD, p);
        call_out = b;
    }
}

void InputHooker::SetTransRect(RECT & g_rect, RECT & b_rect)
{
    TransRect(g_rect, game_rect);
    TransRect(b_rect, back_rect);
}

void InputHooker::SetWindowed(bool b)
{
    windowed = b;
}

HWND InputHooker::GetCurrentHWND()
{
    return cur_hwnd;
}

void InputHooker::CheckHotKey()
{
    //�������̵߳���
    if(GetTickCount() - last_tick < 500 )
        return;
    DWORD window_thread = 0;
    HWND hwnd = GetGameWindow(window_thread);
    if (hwnd)
    {
        //��⵱ǰ���߳��Ƿ���Hwnd�����߳�
        DWORD cur_thread = ::GetCurrentThreadId();
        if(cur_thread != window_thread)
            return;

        if (cur_hwnd != hwnd)
        {
            if (cur_hwnd != NULL)
            {
                UninstallHook();
                cur_hwnd = NULL;
            }

            if(InstallHook(hwnd))
                cur_hwnd = hwnd;
            else
                return;
        }


        //ѭ����⵱ǰ�ȼ��Ƿ���
        BOOL IsHotKey[2] = {0};
        for (int i = 0; i < 2; i++)
        {
            DWORD key_val = hotkey >> (8 * i) & 0xFF;
            if (key_val != 0)
                IsHotKey[i] = (User32Hooker::OrgGetAsyncKeyState(key_val) &
                0x8000) ? TRUE : FALSE;
            else
                IsHotKey[i] = TRUE;
        }


        if(IsHotKey[0] && IsHotKey[1])
        {//�������ȼ�������
            hotkey_pressed = true;
        }

        if(!IsHotKey[0] && !IsHotKey[1])
        {//�ȼ�δ������
            if(hotkey_pressed)
            {//����ǰHOT key�����¹�
                PostMessage(hwnd, WM_CUSTOM_MESSAGE, WPARAM_HOTKEY, 0);
                //����������ȼ�ʱ��
                last_tick = GetTickCount();
                hotkey_pressed = false;
            }
        }
    } 

}

//void TransPoint(int old_x, int old_y, int & new_x, int & new_y)
//{
//    float zoom;
//    uint32_t f_width = game_rect.width();
//    uint32_t f_height = game_rect.height();
//    uint32_t b_width = back_rect.width();
//    uint32_t b_height = back_rect.height();
//    old_x -= game_rect.left();
//    old_y -= game_rect.top();
//
//    if ((float)f_width / b_width > (float)f_height / b_height)
//    {
//        zoom = (float)f_height / b_height;
//        new_x = (int)((old_x - (f_width - b_width * zoom) / 2) / zoom);
//        new_y = (int)(old_y / zoom);
//    } 
//    else
//    {
//        zoom = (float)f_width / b_width;
//        new_x = (int)(old_x / zoom);
//        new_y = static_cast<int>((old_y - 
//            (f_height - b_height * zoom) / 2) / zoom);
//    }
//}

HWND GetGameWindow(DWORD & thread_id)
{
    HWND hwnd = NULL;
    thread_id = 0;

    hwnd = ::GetActiveWindow();
    if (NULL == hwnd) //��hwnd����Ϊ������
        hwnd = ::GetForegroundWindow();

    if (hwnd)
    {
        DWORD pid = 0;
        thread_id = GetWindowThreadProcessId(hwnd, &pid);
        if (pid != ::GetCurrentProcessId())
        {
            thread_id = 0;
            hwnd = 0;
        }
    }

    return hwnd;
}

bool InstallHook(HWND hwnd)
{
    DWORD tid = 0;
    tid = ::GetWindowThreadProcessId(hwnd, NULL);
    hhook = SetWindowsHookEx(WH_GETMESSAGE, GetMsgProc,
        (HINSTANCE)NULL, tid);
    if(hhook == 0)
        return false;
    return true;
}

void UninstallHook()
{
    UnhookWindowsHookEx(hhook);
    hhook = 0;
}

void TransMouseMsg(MSG & msg)
{
    POINT pt;
    pt.x = GET_X_LPARAM(msg.lParam);
    pt.y = GET_Y_LPARAM(msg.lParam);

    if (windowed)
    {//����ģʽ��  ������Ϣ����Ļ����
        if (msg.message == WM_MOUSEWHEEL ||
            msg.message == WM_MOUSEHWHEEL)
            ScreenToClient(cur_hwnd, &pt);
    }
    else
    {//ȫ��ģʽ�� �����Ϣת��Ϊ��Ļ����
        if (msg.message != WM_MOUSEWHEEL &&
            msg.message != WM_MOUSEHWHEEL)
            ClientToScreen(cur_hwnd, &pt);
    }

    //TransPoint(x, y, new_x, new_y);
    msg.lParam = MAKELPARAM(pt.x, pt.y);
}

LRESULT CALLBACK GetMsgProc(
    _In_  int code,
    _In_  WPARAM wParam,
    _In_  LPARAM lParam
    )
{
    if(code < 0)
        return CallNextHookEx(hhook, code, wParam, lParam);

    MSG * msg = (MSG *)lParam; 
    bool remove = (wParam & PM_REMOVE) != 0;

    if ( (msg->hwnd == cur_hwnd || IsChild(cur_hwnd, msg->hwnd)) )
    {
        if(msg->message == WM_CUSTOM_MESSAGE &&
            msg->wParam == WPARAM_SHIELD)
        {
            if(remove)
            {
                int p = (int)msg->lParam;
                if (p == 1)
                {
                    User32Hooker::StartShield();
                    POINT screen_cursor_pos = org_cursor_pos;
                    ::ClientToScreen(cur_hwnd, &screen_cursor_pos);
                    //�����λ�����û�ԭ��
                    User32Hooker::OrgSetCursorPos(screen_cursor_pos.x - 1, 
                        screen_cursor_pos.y -1);
                    //����OVERLAYRENDER���
                    cursor_pos.x = back_rect.width() / 2 -1;
                    cursor_pos.y = back_rect.height() / 2 -1;
                }
                else
                    User32Hooker::EndShield();

                msg->message = WM_NULL;
                return 0;
            }
        }
        else if(msg->message == WM_CUSTOM_MESSAGE &&
            msg->wParam == WPARAM_HOTKEY  )
        {
            //post to render context
            if(remove)
            {
                RenderContext::Get()->HandleHookMsg(*msg);
                msg->message = WM_NULL;
                return 0;
            }
        }
        else if (msg->message >= WM_KEYFIRST &&
            msg->message <= WM_KEYLAST)
        {
            if (call_out)
            {//����removeʱ ������Ϣ��UI����
                TranslateMessage(msg);
                if(remove)
                    RenderContext::Get()->HandleHookMsg(*msg);
                msg->message = WM_NULL;
                return 0;
            }
        }
        else if (msg->message >= WM_MOUSEFIRST &&
            msg->message <= WM_MOUSELAST)
        {
            if (call_out)
            {
                MSG temp = *msg;
                TransMouseMsg(temp);
                if(remove)
                    HandleMouseMessage(temp);
                msg->message = WM_NULL;
                return 0;
            }
        }//else
        else if (msg->message >= WM_NCMOUSEMOVE &&
            msg->message <= WM_NCXBUTTONDBLCLK)
        {
            if (call_out )
            {
                //������ ȫ��ʱ ������Ϸ��WM_NCXXX(���Ϲž���5)
                //ȫ����Ҫͬ������WM_NCXXX
                //���ڻ�ʱ Ϊ��ֹ�϶� ������WM_NCXXXͬʱ���ε�WM_NCXXX
                if(remove && !windowed)
                    HandleMouseMessage(*msg);
                //���ε���Ϣ
                msg->message = WM_NULL;
                return 0;
            }
        }
    }
    return CallNextHookEx(hhook, code, wParam, lParam);
}

void HandleMouseMessage(MSG & msg)
{
    //Ԥ�ڵ����λ����Ļ����
    POINT screen_cursor_pos = org_cursor_pos;
    ::ClientToScreen(cur_hwnd, &screen_cursor_pos);

    //��ǰ���λ�ò��� Ԥ�ڵ����� ���û�ȥ
    POINT cur_screen_cursor_pos = {0};
    User32Hooker::OrgGetCursorPos(&cur_screen_cursor_pos);
    bool  pos_changed =  
        cur_screen_cursor_pos.x != screen_cursor_pos.x ||
        cur_screen_cursor_pos.y != screen_cursor_pos.y;

    if(pos_changed)
    {
        //�����λ�����û�ԭ��
        User32Hooker::OrgSetCursorPos(screen_cursor_pos.x, 
            screen_cursor_pos.y);
    }

    //λ�øı� ����
    bool need_post = pos_changed || 
                    (!pos_changed && msg.message != WM_MOUSEMOVE);

    if(need_post)
    {
        MSG temp = msg;
        int x = 0, y = 0;
        x = GET_X_LPARAM(temp.lParam);
        y = GET_Y_LPARAM(temp.lParam);
        //��ȡ��ֵ �������λ��
        cursor_pos.x += cur_screen_cursor_pos.x - screen_cursor_pos.x;
        cursor_pos.y += cur_screen_cursor_pos.y - screen_cursor_pos.y;
        //�����λ�ý��б߽紦��
        if(cursor_pos.x < 0)
            cursor_pos.x = 0;
        if(cursor_pos.x > back_rect.width())
            cursor_pos.x = back_rect.width();

        if(cursor_pos.y < 0)
            cursor_pos.y = 0;
        if(cursor_pos.y > back_rect.height())
            cursor_pos.y = back_rect.height();

        temp.lParam = MAKELPARAM(cursor_pos.x, cursor_pos.y);
        RenderContext::Get()->HandleHookMsg(temp);
    }
}

ncore::SpinLock InputHooker::locker_;

BOOL WINAPI FPeekMessageA(LPMSG lpMsg,HWND hWnd,
    UINT wMsgFilterMin,UINT wMsgFilterMax,UINT wRemoveMsg)
{
    InputHooker::CheckHotKey();
    return TPeekMessageA(lpMsg,hWnd,wMsgFilterMin,wMsgFilterMax,wRemoveMsg);
}

BOOL WINAPI FPeekMessageW(LPMSG lpMsg,HWND hWnd,
    UINT wMsgFilterMin,UINT wMsgFilterMax,UINT wRemoveMsg)
{
    InputHooker::CheckHotKey();
    return TPeekMessageW(lpMsg,hWnd,wMsgFilterMin,wMsgFilterMax,wRemoveMsg);
}

BOOL WINAPI FGetMessageA(LPMSG lpMsg,HWND hWnd,
    UINT wMsgFilterMin,UINT wMsgFilterMax)
{
    InputHooker::CheckHotKey();
    return TGetMessageA(lpMsg,hWnd,wMsgFilterMin,wMsgFilterMax);
}

BOOL WINAPI FGetMessageW(LPMSG lpMsg,HWND hWnd,
    UINT wMsgFilterMin,UINT wMsgFilterMax)
{
    InputHooker::CheckHotKey();
    return TGetMessageW(lpMsg,hWnd,wMsgFilterMin,wMsgFilterMax);
}

void InputHooker::Hook()
{
    HMODULE module = ::GetModuleHandle(L"user32.dll");
    if(module == 0)
        return;
    if(!locker_.TryAcquire())
        return;
    if(TGetMessageA || TGetMessageW || TPeekMessageW || TPeekMessageA)
        return;
    TGetMessageA = (GetMessageA_T)GetProcAddress(module, "GetMessageA");
    TGetMessageW = (GetMessageW_T)GetProcAddress(module, "GetMessageW");
    TPeekMessageA = (PeekMessageA_T)GetProcAddress(module, "PeekMessageA");
    TPeekMessageW = (PeekMessageW_T)GetProcAddress(module, "PeekMessageW");

    HANDLE curr_thread = GetCurrentThread();
    DetourTransactionBegin();
    DetourUpdateThread(curr_thread);
    DetourAttach(&(PVOID&)TGetMessageA, FGetMessageA);
    DetourAttach(&(PVOID&)TGetMessageW, FGetMessageW);
    DetourAttach(&(PVOID&)TPeekMessageA, FPeekMessageA);
    DetourAttach(&(PVOID&)TPeekMessageW, FPeekMessageW);
    DetourTransactionCommit();

    locker_.Release();

}


}
}