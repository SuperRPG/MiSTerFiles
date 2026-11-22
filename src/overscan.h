#ifndef _OVERSCAN_H_
#define _OVERSCAN_H_

// MiSTerFiles: "Tela / Overscan" adjust screen (MiSTer only)
namespace OverscanScreen
{
   // Shows the adjust screen with a live preview of the safe area.
   // Returns the chosen value (percent) if confirmed, -1 if cancelled.
   // On return the previous overscan is active again; the caller applies the new one.
   int run(void);
}

#endif
