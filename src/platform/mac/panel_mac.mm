// ---------------------------------------------------------------------------
// platform/mac/panel_mac.mm -- a native Cocoa settings panel.
//
// This is deliberately small: four model controls, the master switch and the touchpad-reverse
// switch. It is not a port of the Windows hand-drawn panel. All values go through
// PlatPanelSettingsFn, so the shared extension remains the only owner of settings and SaveSettings.
// ---------------------------------------------------------------------------
#include "../../platform.h"

#import <Cocoa/Cocoa.h>

using plat::PlatPanelSettings;
using plat::PlatPanelSettingsFn;

namespace
{
PlatPanelSettingsFn g_settingsBridge = nullptr;
id g_panel = nil;

NSString *FormatValue(NSInteger index, double value)
{
  switch (index)
  {
  case 0:
    return [NSString stringWithFormat:@"%.0f ms", value];
  case 1:
    return [NSString stringWithFormat:@"%.1f delta", value];
  case 2:
    return [NSString stringWithFormat:@"%.0f delta", value];
  default:
    return [NSString stringWithFormat:@"%.2f x", value];
  }
}
} // namespace

@interface SwsMacPanel : NSWindowController <NSWindowDelegate>
@property(nonatomic, strong) NSArray<NSSlider *> *sliders;
@property(nonatomic, strong) NSArray<NSTextField *> *values;
@property(nonatomic, strong) NSButton *glideCheckbox;
@property(nonatomic, strong) NSButton *reverseCheckbox;
- (void)refreshFromStore;
- (void)valueChanged:(id)sender;
@end

@implementation SwsMacPanel

- (instancetype)init
{
  NSPanel *panel =
      [[NSPanel alloc] initWithContentRect:NSMakeRect(0, 0, 430, 342)
                                styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                                          NSWindowStyleMaskUtilityWindow
                                  backing:NSBackingStoreBuffered
                                    defer:NO];
  self = [super initWithWindow:panel];
  if (!self)
    return nil;

  panel.title = @"Smooth Wheel Scroll";
  panel.delegate = self;
  panel.releasedWhenClosed = NO;
  panel.hidesOnDeactivate = YES;
  panel.level = NSFloatingWindowLevel;
  [panel setFrameAutosaveName:@"SmoothWheelScrollMacSettings"];

  NSView *content = panel.contentView;
  _glideCheckbox = [NSButton checkboxWithTitle:@"Enable smooth scrolling"
                                         target:self
                                         action:@selector(valueChanged:)];
  _glideCheckbox.frame = NSMakeRect(20, 306, 240, 24);
  [content addSubview:_glideCheckbox];

  _reverseCheckbox = [NSButton checkboxWithTitle:@"Reverse touchpad horizontal zoom"
                                          target:self
                                          action:@selector(valueChanged:)];
  _reverseCheckbox.frame = NSMakeRect(20, 278, 330, 24);
  [content addSubview:_reverseCheckbox];

  NSArray<NSString *> *titles = @[
    @"Glide length", @"Slow step", @"Ramp-up", @"Top speed"
  ];
  const double minima[] = {100.0, 1.0, 60.0, 1.0};
  const double maxima[] = {300.0, 10.0, 2000.0, 2.0};
  NSMutableArray<NSSlider *> *sliders = [NSMutableArray arrayWithCapacity:4];
  NSMutableArray<NSTextField *> *values = [NSMutableArray arrayWithCapacity:4];

  for (NSInteger i = 0; i < 4; ++i)
  {
    const CGFloat y = 232.0 - (CGFloat)i * 57.0;
    NSTextField *label = [NSTextField labelWithString:titles[(NSUInteger)i]];
    label.frame = NSMakeRect(20, y + 22, 230, 20);
    [content addSubview:label];

    NSTextField *value = [NSTextField labelWithString:@""];
    value.alignment = NSTextAlignmentRight;
    value.textColor = NSColor.secondaryLabelColor;
    value.frame = NSMakeRect(260, y + 22, 150, 20);
    [content addSubview:value];
    [values addObject:value];

    NSSlider *slider = [NSSlider sliderWithValue:minima[i]
                                        minValue:minima[i]
                                        maxValue:maxima[i]
                                          target:self
                                          action:@selector(valueChanged:)];
    slider.tag = i;
    slider.continuous = YES;
    slider.frame = NSMakeRect(20, y, 390, 22);
    [content addSubview:slider];
    [sliders addObject:slider];
  }
  _sliders = sliders;
  _values = values;

  NSTextField *note =
      [NSTextField labelWithString:@"Changes apply immediately and are stored in REAPER ExtState."];
  note.textColor = NSColor.secondaryLabelColor;
  note.font = [NSFont systemFontOfSize:11];
  note.frame = NSMakeRect(20, 20, 390, 18);
  [content addSubview:note];

  [self refreshFromStore];
  [panel center];
  return self;
}

- (void)refreshFromStore
{
  PlatPanelSettings settings;
  if (g_settingsBridge)
    g_settingsBridge(&settings, false);

  _glideCheckbox.state = settings.glideOn ? NSControlStateValueOn : NSControlStateValueOff;
  _reverseCheckbox.state =
      settings.touchpadReverse ? NSControlStateValueOn : NSControlStateValueOff;
  const double values[] = {
    settings.windowMs, settings.startDeltas, settings.budgetDeltas, settings.speedMul
  };
  for (NSInteger i = 0; i < 4; ++i)
  {
    NSSlider *slider = _sliders[(NSUInteger)i];
    NSTextField *field = _values[(NSUInteger)i];
    slider.doubleValue = values[i];
    field.stringValue = FormatValue(i, values[i]);
  }
}

- (void)valueChanged:(id)sender
{
  (void)sender;
  PlatPanelSettings settings;
  settings.glideOn = _glideCheckbox.state == NSControlStateValueOn;
  settings.touchpadReverse = _reverseCheckbox.state == NSControlStateValueOn;
  settings.windowMs = ((NSSlider *)_sliders[0]).doubleValue;
  settings.startDeltas = ((NSSlider *)_sliders[1]).doubleValue;
  settings.budgetDeltas = ((NSSlider *)_sliders[2]).doubleValue;
  settings.speedMul = ((NSSlider *)_sliders[3]).doubleValue;
  if (g_settingsBridge)
    g_settingsBridge(&settings, true);

  const double values[] = {
    settings.windowMs, settings.startDeltas, settings.budgetDeltas, settings.speedMul
  };
  for (NSInteger i = 0; i < 4; ++i)
  {
    NSTextField *field = _values[(NSUInteger)i];
    field.stringValue = FormatValue(i, values[i]);
  }
}

@end

namespace plat
{
void PanelBindSettings(PlatPanelSettingsFn fn) { g_settingsBridge = fn; }

bool PlatPanelPresent() { return true; }

void PanelShow()
{
  if (!g_panel)
    g_panel = [[SwsMacPanel alloc] init];
  [(SwsMacPanel *)g_panel refreshFromStore];
  [(SwsMacPanel *)g_panel showWindow:nil];
  [[(SwsMacPanel *)g_panel window] makeKeyAndOrderFront:nil];
}

void PanelToggle()
{
  if (!g_panel)
  {
    PanelShow();
    return;
  }
  NSWindow *window = [(SwsMacPanel *)g_panel window];
  if (window.visible)
    [(SwsMacPanel *)g_panel close];
  else
    PanelShow();
}

bool PanelVisible()
{
  if (!g_panel)
    return false;
  NSWindow *window = [(SwsMacPanel *)g_panel window];
  return window.visible;
}

void PanelRefresh()
{
  if (g_panel)
    [(SwsMacPanel *)g_panel refreshFromStore];
}

void PanelShutdown()
{
  if (g_panel)
  {
    [(SwsMacPanel *)g_panel close];
    g_panel = nil;
  }
  g_settingsBridge = nullptr;
}

} // namespace plat
