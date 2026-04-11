# Pottery — Guide d'architecture et d'utilisation

## La stack technologique

```
┌─────────────────────────────────────────────────────┐
│                  Application                        │
│         pottery_mold_button(), pottery_mold_list()  │
├─────────────────────────────────────────────────────┤
│              Pottery  (ce projet)                   │
│   Widgets · State map · Toolbar · Statusbar         │
├──────────────────┬──────────────────────────────────┤
│      Clay        │           Cairo                  │
│   Layout engine  │      Rendu vectoriel 2D          │
│   (positions,    │   (rectangles, texte, SVG)       │
│    tailles)      │                                  │
├──────────────────┤      Pango  (texte)              │
│                  │      librsvg (SVG/icônes)        │
├──────────────────┴──────────────────────────────────┤
│              Backend Win32 (ou X11, Cocoa)          │
│         Fenêtre · Événements · Présentation         │
└─────────────────────────────────────────────────────┘
```

Chaque couche a une responsabilité unique :

| Couche | Rôle | Ce qu'elle NE fait PAS |
|--------|------|----------------------|
| **Clay** | Calcule positions et tailles | Dessiner |
| **Cairo** | Dessine pixels, courbes, texte | Layout |
| **Pango** | Mesure et dessine le texte | Autre chose |
| **librsvg** | Rend les icônes SVG | Autre chose |
| **Pottery** | Gère l'état, orchestre tout | Calcul de layout |
| **Backend** | Fenêtre OS, événements | Rendu |

---

## Immediate mode vs Retained mode

Pottery est un **hybride** — c'est important à comprendre.

### Clay : immediate mode pur

Clay recalcule le layout **entièrement à chaque frame**. Si tu n'appelles pas
`pottery_mold_button(kiln, "mon_btn", ...)` dans un frame, ce bouton n'existe
pas ce frame-là. C'est la même philosophie que Dear ImGui.

```c
// Frame N : le bouton existe
pottery_mold_button(kiln, "ok", "OK", NULL);

// Frame N+1 : condition false → le bouton disparaît automatiquement
if (show_ok)
    pottery_mold_button(kiln, "ok", "OK", NULL);
```

**Avantage** : l'UI suit automatiquement l'état de l'application.
Pas de synchronisation manuelle, pas de callbacks complexes.

### Pottery : retained mode pour l'état

Contrairement à ImGui pur, Pottery maintient un **état persistant** par widget
dans le `PotteryStateMap` — une hash table indexée par l'ID string du widget.

```
PotteryStateMap (hash table ouverte)
  "name_edit"  → PotteryWidgetState { focused=true, edit.cursor=5, ... }
  "file_list"  → PotteryWidgetState { list.scroll_y=120.0, ... }
  "lang_combo" → PotteryWidgetState { combo.open=false, ... }
```

Cet état **survit entre les frames** : le texte tapé dans un Edit, la position
de scroll d'une List, l'item sélectionné — tout est conservé automatiquement.
L'application n'a pas besoin de stocker cet état elle-même.

---

## Cycle d'un frame

```
┌─────────────────────────────────────────────────────────────────┐
│  pottery_kiln_begin_frame(kiln)                                 │
│                                                                 │
│  1. pottery_input_reset_frame()                                 │
│     Efface les événements one-shot du frame précédent :        │
│     mouse_clicked=false, wheel_dy=0, char_queue vide...        │
│                                                                 │
│  2. backend->poll_event()  ← pomper la file Win32              │
│     WM_MOUSEMOVE  → input.mouse_x/y                            │
│     WM_LBUTTONUP  → input.mouse_clicked[0] = true              │
│     WM_MOUSEWHEEL → input.wheel_dy                             │
│     WM_CHAR       → input.char_queue[]                         │
│     WM_SIZE       → resize Cairo + Clay_SetLayoutDimensions    │
│                                                                 │
│  3. pottery_toolbar_handle_event()                              │
│     La toolbar consomme les events souris dans sa zone          │
│     avant qu'ils n'atteignent Clay                              │
│                                                                 │
│  4. mouse_y -= toolbar_height                                   │
│     Recalibrage : Clay travaille en coordonnées relatives       │
│     à sa zone (sous la toolbar), pas en coordonnées écran      │
│                                                                 │
│  5. Clay_SetPointerState()  ← informer Clay de la souris       │
│  6. Clay_UpdateScrollContainers()  ← transmettre la molette    │
│  7. Clay_BeginLayout()  ← ouvrir la déclaration UI             │
└─────────────────────────────────────────────────────────────────┘
                            ↓
┌─────────────────────────────────────────────────────────────────┐
│  Application : build_ui(kiln, &app)                            │
│                                                                 │
│  POTTERY_VESSEL(kiln, &root) {                                  │
│      pottery_mold_label(...)                                    │
│      pottery_mold_edit(...)                                     │
│      if (pottery_mold_button(...)) do_something();              │
│  }                                                              │
│                                                                 │
│  Chaque mold :                                                  │
│  a) Récupère/crée son état dans le StateMap                    │
│  b) Déclare un élément Clay (CUSTOM, TEXT, ou RECTANGLE)       │
│  c) Lit le bounding box du frame PRÉCÉDENT                     │
│  d) Met à jour son état (hover, pressed, focused...)           │
│  e) Retourne un booléen si action (clic, changement...)        │
└─────────────────────────────────────────────────────────────────┘
                            ↓
┌─────────────────────────────────────────────────────────────────┐
│  pottery_kiln_end_frame(kiln)                                   │
│                                                                 │
│  1. Clay_EndLayout()                                            │
│     → Clay calcule TOUTES les positions et tailles             │
│     → Retourne Clay_RenderCommandArray                         │
│                                                                 │
│  2. Effacer la zone Clay (entre toolbar et statusbar)           │
│     cairo_rectangle() + cairo_fill() avec glaze.background     │
│                                                                 │
│  3. cairo_translate(0, toolbar_height)                          │
│     Décaler Cairo : coordonnées Clay 0,0 = sous la toolbar     │
│                                                                 │
│  4. pottery_renderer_fire()  ← parcourir les commandes Clay    │
│     RECTANGLE → cairo_fill()                                   │
│     TEXT      → pango_cairo_show_layout()                      │
│     BORDER    → cairo_stroke()                                  │
│     CUSTOM    → draw_button() / draw_edit() / draw_list()      │
│     SCISSOR   → cairo_clip() / cairo_restore()                 │
│                                                                 │
│  5. pottery_kiln_render_toolbar()   ← Cairo direct, par-dessus │
│  6. pottery_kiln_render_statusbar() ← Cairo direct, par-dessus │
│                                                                 │
│  7. cairo_surface_flush()  ← forcer écriture dans le DC Win32  │
│  8. backend->present()  ← BitBlt vers la fenêtre écran        │
│                                                                 │
│  9. pottery_state_map_gc()                                      │
│     Supprimer les états des widgets non déclarés ce frame      │
│     (widget disparu de l'UI → état libéré automatiquement)     │
└─────────────────────────────────────────────────────────────────┘
```

---

## Anatomie d'un mold

Un mold fait **deux choses simultanément** :
1. **Déclarer** à Clay ce qu'il faut placer et sa taille
2. **Répondre** aux événements sur ce widget

```c
bool pottery_mold_button(PotteryKiln *kiln, const char *id,
                          const char *label,
                          const PotteryButtonOpts *opts) {

    // ① Récupérer/créer l'état persistant
    uint64_t wid = pottery_hash_string(id);
    PotteryWidgetState *state = pottery_state_map_get_or_create(
        &kiln->state_map, wid, POTTERY_WIDGET_BUTTON);
    state->alive = true;  // ← "vu ce frame", évite le GC

    // ② Allouer un payload dans le pool du frame
    //    (automatiquement libéré au begin_frame suivant)
    PotteryCustomPayload *payload = pottery_payload_alloc(&kiln->payload_pool);
    payload->type         = POTTERY_CUSTOM_BUTTON;
    payload->button.label = label;
    payload->state        = state;

    // ③ Déclarer l'élément à Clay
    Clay_ElementDeclaration decl = {0};
    decl.layout.sizing.height = CLAY_SIZING_FIXED(28);
    decl.layout.sizing.width  = CLAY_SIZING_FIXED(80);
    decl.custom.customData    = payload;  // transmis au renderer

    Clay__OpenElementWithId(POTTERY_ID(id));
    Clay__ConfigureOpenElement(decl);
    Clay__CloseElement();

    // ④ Lire le bounding box du frame PRÉCÉDENT
    Clay_ElementData data = Clay_GetElementData(POTTERY_ID(id));
    bool clicked = false;

    if (data.found) {
        bool hovered = point_in_bb(data.boundingBox,
                                    kiln->input.mouse_x,
                                    kiln->input.mouse_y);
        state->hovered = hovered;

        // ⑤ Détecter clic = relâchement souris dans la zone
        if (hovered && kiln->input.mouse_clicked[0]) {
            clicked = true;
            kiln->input.focused_id = wid;
        }
        state->focused = (kiln->input.focused_id == wid);
    }

    return clicked;  // ← l'app agit sur true
}
```

### La latence d'un frame

Clay calcule les bounding boxes dans `EndLayout` (étape 1 de `end_frame`).
Le mold lit ces boxes au **frame suivant** via `Clay_GetElementData`.
Un frame de latence ≈ 16ms à 60fps — imperceptible pour l'utilisateur.
Au premier lancement, le premier frame n'a pas de hit-test. Dès le deuxième,
tout fonctionne normalement.

---

## Le mécanisme CUSTOM

Pour les widgets complexes, Pottery utilise le type `CUSTOM` de Clay.
Clay ne sait pas dessiner un bouton — il délègue au renderer Pottery.

```
Mold                    Clay                      Renderer
─────────────────────────────────────────────────────────────
pottery_mold_button()
  payload = { label, state, ... }
  decl.custom.customData = payload
  Clay__ConfigureOpenElement(decl)
                    ↓
         [Clay calcule position et taille]
                    ↓
         Clay_RenderCommand {
           commandType = CUSTOM,
           boundingBox = { x=16, y=80, w=80, h=28 },
           renderData.custom.customData = payload
         }
                    ↓
                              pottery_renderer_fire()
                                case CUSTOM:
                                  draw_button(cr, bb, payload)
                                    cairo_fill()    ← fond coloré
                                    cairo_stroke()  ← bordure
                                    pango_show()    ← label centré
```

---

## Le State Map

```
pottery_hash_string("name_edit")  →  0xA3F2B7...  (FNV-1a 64 bits)
                                            ↓
┌────────────────────────────────────────────────────────────┐
│  PotteryStateMap  (open-addressing, capacité power-of-2)  │
│                                                            │
│  slot 0  : vide                                            │
│  slot 1  : id=0xA3F2  type=EDIT                           │
│              hovered=false  focused=true  alive=true       │
│              edit.stb_state → STB_TexteditState            │
│              edit.cursor_pos = 5                           │
│              edit.scroll_offset = 0                        │
│  slot 2  : vide                                            │
│  slot 3  : id=0x7B1C  type=LIST                           │
│              list.scroll_y = 120.0                         │
│  ...                                                       │
└────────────────────────────────────────────────────────────┘

GC en fin de frame (pottery_state_map_gc) :
  alive=true  → widget déclaré ce frame → conserver
  alive=false → widget absent ce frame  → supprimer
```

---

## Toolbar et Statusbar — Cairo direct

Contrairement aux widgets Clay, toolbar et statusbar sont dessinées
**directement avec Cairo** après le rendu Clay. Pourquoi ?

- **Hauteur fixe** → pas besoin du moteur de layout Clay
- **Toujours visibles** → pas de scroll, pas de clip
- **Dessinées en dernier** → garantit qu'elles sont par-dessus tout

```
y=0    ┌────────────────────────────────┐
       │  TOOLBAR  (Cairo direct, 36px) │
y=36   ├────────────────────────────────┤
       │                                │
       │  ZONE CLAY                     │
       │  height = H - 36 - 22         │
       │  Cairo translaté de +36px     │
       │                                │
y=H-22 ├────────────────────────────────┤
       │  STATUSBAR (Cairo direct, 22px)│
y=H    └────────────────────────────────┘
```

---

## Règles essentielles

**① L'ID est unique et stable**
Deux widgets avec le même ID partagent le même état — bug garanti.
Un ID qui change entre les frames = état perdu à chaque frame.

**② `CLAY_STRING` vs `POTTERY_ID`**

```c
// ✓ Littéral connu à la compilation
Clay__OpenElementWithId(Clay_GetElementId(CLAY_STRING("mon_btn")));

// ✓ Variable dynamique
Clay__OpenElementWithId(POTTERY_ID(id));  // où id est const char*

// ✗ Erreur de compilation
Clay__OpenElementWithId(Clay_GetElementId(CLAY_STRING(id)));
```

**③ Payload pool = durée d'un frame**
Les payloads CUSTOM sont alloués dans `kiln->payload_pool`, réinitialisé
à chaque `begin_frame`. Ne jamais garder un pointeur de payload d'un frame
à l'autre.

**④ Compound literals et macros**
```c
// ✗ Ne compile pas — virgules = arguments séparés de la macro
POTTERY_VESSEL(kiln, &(PotteryVesselDesc){ .id="r", .gap=8 });

// ✓ Déclarer en variable locale d'abord
PotteryVesselDesc desc = { .id = "root", .gap = 8 };
POTTERY_VESSEL(kiln, &desc) { ... }
```

**⑤ Cairo direct pour les zones fixes**
Toolbar, statusbar, overlays — Cairo direct.
Contenu scrollable et redimensionnable — Clay + Pottery molds.

---

## Intégration dans une application existante

Si l'application a déjà un canvas Win32 (comme le BIM engine), Pottery
gère l'UI **autour** du canvas. Le canvas reste une fenêtre Win32 enfant
avec son propre `WM_PAINT` — Pottery ne le touche pas.

```
Fenêtre parente Win32
  ├── Pottery (toolbar + Clay content + statusbar)
  └── Canvas Win32 enfant (rendu CAD/GIS indépendant)
```

Pottery redessine l'UI au rythme de sa boucle.
Le canvas se redessine uniquement quand les données changent (`InvalidateRect`).
Les deux sont indépendants.

---

*Pottery v0.1 — Clay · Cairo · Pango · librsvg · stb_textedit*
