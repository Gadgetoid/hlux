#include <windows.h>
#include <commctrl.h>

#define APP_TITLE L"hlux"
#define SETTINGS_KEY L"Software\\hlux"

#define LCDC_PHYSICAL_PAGE 0x10001000u
#define LCDC_PAGE_SIZE     0x1000u
#define CPTWAR_OFFSET      0x30u
#define CPTWDR_OFFSET      0x32u
#define CPTRAR_OFFSET      0x34u
#define CPTRDR_OFFSET      0x36u
#define PALETTE_SIZE       256

#define ROW_HEIGHT    30
#define ROW_TOP       8
#define LABEL_LEFT    8
#define LABEL_WIDTH   86
#define SLIDER_LEFT   96
#define SLIDER_WIDTH  300
#define VALUE_LEFT    400
#define VALUE_WIDTH   46
#define PRESET_LEFT   456
#define PRESET_WIDTH  84
#define PRESET_HEIGHT 26
#define PRESET_GAP    6

#define ID_SLIDER 100
#define ID_VALUE  200
#define ID_PRESET 300

typedef BOOL (WINAPI *VirtualCopyFunction)(LPVOID destination, LPVOID source, DWORD size, DWORD protection);

enum { TEMPERATURE, TINT, HUE, SATURATION, CONTRAST, BRIGHTNESS, SETTING_COUNT };

typedef struct {
    const WCHAR *name;
    int minimum, maximum, neutral;
    const WCHAR *unit;
} setting_t;

static const setting_t settings[SETTING_COUNT] = {
    { L"Temperature", -100, 100, 0, L"" },
    { L"Tint", -100, 100, 0, L"" },
    { L"Hue", -180, 180, 0, L"\x00B0" },
    { L"Saturation", 0, 200, 100, L"%" },
    { L"Contrast", 0, 200, 100, L"%" },
    { L"Brightness", -100, 100, 0, L"" },
};

typedef struct {
    const WCHAR *name;
    int values[SETTING_COUNT];
} preset_t;

static const preset_t presets[] = {
    { L"Normal", { 0, 0, 0, 100, 100, 0 } },
    { L"Warm", { 45, 0, 0, 100, 100, 0 } },
    { L"Cool", { -45, 0, 0, 100, 100, 0 } },
    { L"Monochrome", { 0, 0, 0, 0, 100, 0 } },
    { L"Sepia", { 70, 0, 0, 0, 90, 0 } },
    { L"Night", { 100, 0, 0, 70, 90, -25 } },
    { L"Vivid", { 0, 0, 0, 160, 115, 0 } },
    { L"Faded", { 0, 0, 0, 55, 75, 10 } },
};

#define PRESET_COUNT ((int)(sizeof presets / sizeof presets[0]))

static const short sine_table[91] = {
    0, 17, 35, 52, 70, 87, 105, 122, 139, 156, 174, 191, 208, 225, 242, 259, 276, 292, 309, 326, 342, 358, 375, 391,
    407, 423, 438, 454, 469, 485, 500, 515, 530, 545, 559, 574, 588, 602, 616, 629, 643, 656, 669, 682, 695, 707,
    719, 731, 743, 755, 766, 777, 788, 799, 809, 819, 829, 839, 848, 857, 866, 875, 883, 891, 899, 906, 914, 921,
    927, 934, 940, 946, 951, 956, 961, 966, 970, 974, 978, 982, 985, 988, 990, 993, 995, 996, 998, 999, 999, 1000,
    1000,
};

static int current[SETTING_COUNT];
static PALETTEENTRY base_palette[PALETTE_SIZE];
static volatile BYTE *lcdc_page;
static HWND sliders[SETTING_COUNT];
static HWND value_labels[SETTING_COUNT];

static int sine(int degrees) {
    degrees %= 360;
    if (degrees < 0) degrees += 360;
    if (degrees <= 90) return sine_table[degrees];
    if (degrees <= 180) return sine_table[180 - degrees];
    if (degrees <= 270) return -sine_table[degrees - 180];
    return -sine_table[360 - degrees];
}

static int cosine(int degrees) {
    return sine(degrees + 90);
}

static int clamp(int value, int low, int high) {
    return value < low ? low : value > high ? high : value;
}

static void write_register(DWORD offset, WORD value) {
    *(volatile WORD *)(lcdc_page + offset) = value;
}

static WORD read_register(DWORD offset) {
    return *(volatile WORD *)(lcdc_page + offset);
}

static BOOL map_lcdc(void) {
    HMODULE coredll = LoadLibraryW(L"coredll.dll");
    VirtualCopyFunction virtual_copy = coredll ? (VirtualCopyFunction)GetProcAddressW(coredll, L"VirtualCopy") : 0;
    if (!virtual_copy) return FALSE;
    BYTE *page = VirtualAlloc(0, LCDC_PAGE_SIZE, MEM_RESERVE, PAGE_NOACCESS);
    if (!page) return FALSE;
    if (!virtual_copy(page, (LPVOID)(LCDC_PHYSICAL_PAGE >> 8), LCDC_PAGE_SIZE, PAGE_READWRITE | PAGE_NOCACHE | PAGE_PHYSICAL)) {
        VirtualFree(page, 0, MEM_RELEASE);
        return FALSE;
    }
    lcdc_page = page;
    return TRUE;
}

static BOOL read_system_palette(void) {
    HDC screen = GetDC(0);
    UINT count = screen ? GetSystemPaletteEntries(screen, 0, PALETTE_SIZE, base_palette) : 0;
    if (screen) ReleaseDC(0, screen);
    return count == PALETTE_SIZE;
}

static void read_hardware_palette(void) {
    for (int index = 0; index < PALETTE_SIZE; index++) {
        write_register(CPTRAR_OFFSET, (WORD)(index << 8));
        base_palette[index].peRed = (BYTE)((read_register(CPTRDR_OFFSET) & 0x3F) << 2);
        base_palette[index].peGreen = (BYTE)((read_register(CPTRDR_OFFSET) & 0x3F) << 2);
        base_palette[index].peBlue = (BYTE)((read_register(CPTRDR_OFFSET) & 0x3F) << 2);
    }
}

static void adjust(const PALETTEENTRY *in, int *red, int *green, int *blue) {
    int r = in->peRed, g = in->peGreen, b = in->peBlue;

    int angle = current[HUE];
    if (angle) {
        int c = cosine(angle), s = sine(angle);
        int rr = 299000 + 701 * c + 168 * s, rg = 587000 - 587 * c + 330 * s, rb = 114000 - 114 * c - 497 * s;
        int gr = 299000 - 299 * c - 328 * s, gg = 587000 + 413 * c + 35 * s, gb = 114000 - 114 * c + 292 * s;
        int br = 299000 - 300 * c + 1250 * s, bg = 587000 - 588 * c - 1050 * s, bb = 114000 + 886 * c - 203 * s;
        int nr = (rr / 1000 * r + rg / 1000 * g + rb / 1000 * b) / 1000;
        int ng = (gr / 1000 * r + gg / 1000 * g + gb / 1000 * b) / 1000;
        int nb = (br / 1000 * r + bg / 1000 * g + bb / 1000 * b) / 1000;
        r = nr;
        g = ng;
        b = nb;
    }

    int luma = (299 * r + 587 * g + 114 * b) / 1000;
    r = luma + (r - luma) * current[SATURATION] / 100;
    g = luma + (g - luma) * current[SATURATION] / 100;
    b = luma + (b - luma) * current[SATURATION] / 100;

    r = r * (1000 + 3 * current[TEMPERATURE]) / 1000;
    g = g * (1000 + current[TEMPERATURE] * 2 / 5 - 3 * current[TINT]) / 1000;
    b = b * (1000 - 3 * current[TEMPERATURE]) / 1000;

    r = 128 + (r - 128) * current[CONTRAST] / 100;
    g = 128 + (g - 128) * current[CONTRAST] / 100;
    b = 128 + (b - 128) * current[CONTRAST] / 100;

    int lift = current[BRIGHTNESS] * 255 / 100;
    *red = clamp(r + lift, 0, 255);
    *green = clamp(g + lift, 0, 255);
    *blue = clamp(b + lift, 0, 255);
}

static void apply_palette(void) {
    for (int index = 0; index < PALETTE_SIZE; index++) {
        int red, green, blue;
        adjust(&base_palette[index], &red, &green, &blue);
        write_register(CPTWAR_OFFSET, (WORD)(index << 8));
        write_register(CPTWDR_OFFSET, (WORD)(red >> 2));
        write_register(CPTWDR_OFFSET, (WORD)(green >> 2));
        write_register(CPTWDR_OFFSET, (WORD)(blue >> 2));
    }
}

static void load_settings(void) {
    HKEY key;
    for (int index = 0; index < SETTING_COUNT; index++) current[index] = settings[index].neutral;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, SETTINGS_KEY, 0, 0, &key) != ERROR_SUCCESS) return;
    for (int index = 0; index < SETTING_COUNT; index++) {
        DWORD value, size = sizeof value, type;
        if (RegQueryValueExW(key, settings[index].name, 0, &type, (BYTE *)&value, &size) == ERROR_SUCCESS && type == REG_DWORD) {
            current[index] = clamp((int)value, settings[index].minimum, settings[index].maximum);
        }
    }
    RegCloseKey(key);
}

static void save_settings(void) {
    HKEY key;
    DWORD disposition;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, SETTINGS_KEY, 0, 0, 0, 0, 0, &key, &disposition) != ERROR_SUCCESS) return;
    for (int index = 0; index < SETTING_COUNT; index++) {
        DWORD value = (DWORD)current[index];
        RegSetValueExW(key, settings[index].name, 0, REG_DWORD, (const BYTE *)&value, sizeof value);
    }
    RegCloseKey(key);
}

static void show_value(int index) {
    WCHAR text[16];
    wsprintfW(text, L"%d%s", current[index], settings[index].unit);
    SetWindowTextW(value_labels[index], text);
}

static void show_all(void) {
    for (int index = 0; index < SETTING_COUNT; index++) {
        SendMessageW(sliders[index], TBM_SETPOS, TRUE, current[index] - settings[index].minimum);
        show_value(index);
    }
}

static void create_controls(HWND window, HINSTANCE instance) {
    for (int index = 0; index < SETTING_COUNT; index++) {
        int top = ROW_TOP + index * ROW_HEIGHT;
        CreateWindowW(L"STATIC", settings[index].name, WS_CHILD | WS_VISIBLE, LABEL_LEFT, top + 5, LABEL_WIDTH, 18, window, 0, instance, 0);
        sliders[index] = CreateWindowW(TRACKBAR_CLASS, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | TBS_HORZ | TBS_NOTICKS,
                                       SLIDER_LEFT, top, SLIDER_WIDTH, 26, window, (HMENU)(ID_SLIDER + index), instance, 0);
        SendMessageW(sliders[index], TBM_SETRANGEMIN, FALSE, 0);
        SendMessageW(sliders[index], TBM_SETRANGEMAX, FALSE, settings[index].maximum - settings[index].minimum);
        SendMessageW(sliders[index], TBM_SETPAGESIZE, 0, 10);
        value_labels[index] = CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_RIGHT, VALUE_LEFT, top + 5, VALUE_WIDTH, 18,
                                            window, (HMENU)(ID_VALUE + index), instance, 0);
    }
    for (int index = 0; index < PRESET_COUNT; index++) {
        int left = PRESET_LEFT + (index % 2) * (PRESET_WIDTH + PRESET_GAP);
        int top = ROW_TOP + (index / 2) * (PRESET_HEIGHT + PRESET_GAP);
        CreateWindowW(L"BUTTON", presets[index].name, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                      left, top, PRESET_WIDTH, PRESET_HEIGHT, window, (HMENU)(ID_PRESET + index), instance, 0);
    }
    show_all();
}

static LRESULT CALLBACK window_procedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
    case WM_CREATE:
        create_controls(window, ((CREATESTRUCTW *)lparam)->hInstance);
        return 0;
    case WM_HSCROLL:
        for (int index = 0; index < SETTING_COUNT; index++) {
            if ((HWND)lparam != sliders[index]) continue;
            int position = (int)SendMessageW(sliders[index], TBM_GETPOS, 0, 0) + settings[index].minimum;
            if (position == current[index]) break;
            current[index] = position;
            show_value(index);
            apply_palette();
            break;
        }
        return 0;
    case WM_COMMAND: {
        int id = LOWORD(wparam);
        if (id >= ID_PRESET && id < ID_PRESET + PRESET_COUNT) {
            for (int index = 0; index < SETTING_COUNT; index++) current[index] = presets[id - ID_PRESET].values[index];
            show_all();
            apply_palette();
        }
        return 0;
    }
    case WM_DESTROY:
        save_settings();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

int WINAPI WinMain(HINSTANCE instance, HINSTANCE previous, LPWSTR command_line, int show) {
    (void)previous;
    (void)command_line;

    HWND existing = FindWindowW(APP_TITLE, 0);
    if (existing) {
        SetForegroundWindow(existing);
        return 0;
    }
    if (!map_lcdc()) {
        MessageBoxW(0, L"Could not map the HD64461 display controller.", APP_TITLE, MB_OK | MB_ICONEXCLAMATION);
        return 1;
    }
    if (!read_system_palette()) read_hardware_palette();
    load_settings();
    apply_palette();

    INITCOMMONCONTROLSEX controls = { sizeof controls, ICC_BAR_CLASSES };
    InitCommonControlsEx(&controls);

    WNDCLASSW window_class = { 0 };
    window_class.lpfnWndProc = window_procedure;
    window_class.hInstance = instance;
    window_class.hbrBackground = (HBRUSH)GetStockObject(LTGRAY_BRUSH);
    window_class.lpszClassName = APP_TITLE;
    if (!RegisterClassW(&window_class)) return 1;

    RECT work;
    if (!SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0)) SetRect(&work, 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN));
    HWND window = CreateWindowW(APP_TITLE, APP_TITLE, WS_VISIBLE | WS_CAPTION | WS_SYSMENU, work.left, work.top,
                                work.right - work.left, work.bottom - work.top, 0, 0, instance, 0);
    if (!window) return 1;
    ShowWindow(window, show);
    UpdateWindow(window);

    MSG message;
    while (GetMessageW(&message, 0, 0, 0)) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return 0;
}
