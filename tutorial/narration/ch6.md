# ch6

Floor plans let you control your home from a drawing of it.

You start by creating a floor plan and uploading an SVG drawing. Any tool that exports SVG will do, including drawings exported from floor plan apps.

Uploaded drawings are checked twice. The web app sanitises the SVG, and the firmware validates it again before storing it. Scripts, external links and other unsafe content are rejected, never silently repaired.

Your drawing is never modified. The positions of lights and room outlines are stored separately, in the drawing's own coordinate system, so they line up on every screen size.

In control mode, each light sits on the plan as a dot with a soft glow around it.

The glow is not an animation the browser invents. Its colour and brightness come from the actual output of that light, as reported by the controller.

Tap a light to switch it on or off. A long press, a right click, or the i key opens its full controls. You can pan, zoom and go full screen.

Edit mode is a separate editor, built for arranging things. New lights appear in a tray, and you drag them onto the plan.

You can resize the glow of each light, pick its colour, and draw room outlines so the plan knows where each room is.

The editor has snapping to a grid, alignment and distribution tools, and undo and redo. Nothing is saved until you press Save, and you can discard your changes at any time.

A controller can hold up to three floor plans, for example one per floor.
