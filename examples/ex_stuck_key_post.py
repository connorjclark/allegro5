"""Post key messages to the ex_stuck_key window to simulate a Windows key up
whose "previous key state" bit (lParam bit 30) is clear.

    python ex_stuck_key_post.py            # arrow Right, bug case (bit 30 clear)
    python ex_stuck_key_post.py --normal   # same, bit 30 set (control case)
    python ex_stuck_key_post.py --key z    # non-extended key
"""
import ctypes, sys, time

u = ctypes.windll.user32
KEYS = {  # name: (virtual key, scancode, extended)
    'right': (0x27, 0x4D, True), 'left': (0x25, 0x4B, True),
    'up': (0x26, 0x48, True), 'down': (0x28, 0x50, True),
    'z': (0x5A, 0x2C, False), 'lctrl': (0x11, 0x1D, False),
}
name = 'right'
if '--key' in sys.argv:
    name = sys.argv[sys.argv.index('--key') + 1].lower()
vk, scan, ext = KEYS[name]
normal = '--normal' in sys.argv

hwnd = u.FindWindowW(None, "ex_stuck_key")
assert hwnd, "ex_stuck_key window not found"
WM_KEYDOWN, WM_KEYUP = 0x100, 0x101
lparam = 1 | (scan << 16) | ((1 << 24) if ext else 0)
up = lparam | (1 << 31) | ((1 << 30) if normal else 0)

u.SetForegroundWindow(hwnd)
time.sleep(0.5)
print(f"posting KEYDOWN {name}")
u.PostMessageW(hwnd, WM_KEYDOWN, vk, lparam)
time.sleep(1.0)
print(f"posting KEYUP {name} with bit 30 {'set' if normal else 'CLEAR'}")
u.PostMessageW(hwnd, WM_KEYUP, vk, up)
