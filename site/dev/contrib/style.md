Coding Style Guidelines
=======================

These conventions have evolved over time. Some of the earlier code in both
projects doesn't strictly adhere to the guidelines. However, as the code evolves
we hope to make the existing code conform to the guildelines.

Files
-----

We use .cpp and .h as extensions for c++ source and header files.

Headers that aren't meant for public consumption should be placed in src
directories so that they aren't in a client's search path, or in
include/private if they need to be used by public headers.

We prefer to minimize includes. If forward declaring a name in a header is
sufficient then that is preferred to an include.

Forward declarations and file includes should be in alphabetical order (but we
aren't very strict about it).

<span id="no-define-before-sktypes"></span>
Do not use #if/#ifdef before including "SkTypes.h" (directly or indirectly).
Most things you'd #if on tend to not yet be decided until SkTypes.h.

We use 4 spaces, not tabs.

We use Unix style endlines (LF).

We prefer no trailing whitespace but aren't very strict about it.

We wrap lines at 100 columns unless it is excessively ugly (use your judgement).
The soft line length limit was changed from 80 to 100 columns in June 2012. Thus,
many files still adhere to the 80 column limit. It is not necessary or worth
significant effort to promote 80 column wrapped files to 100 columns. Please
don't willy-nilly insert longer lines in 80 column wrapped files. Either be
consistent with the surrounding code or, if you really feel the need, promote
the surrounding code to 100 column wrapping.

Naming
------

Both projects use a prefix to designate that they are Skia prefix for classes,
enums, structs, typedefs etc is Sk. Ganesh's is Gr. Nested types should not be
prefixed.

<!--?prettify?-->
~~~~
class SkClass {
public:
    class HelperClass {
        ...
    };
};
~~~~

Data fields in structs, classes, unions begin with lower-case f and are then
camel-capped.

<!--?prettify?-->
~~~~
struct GrCar {
    ...
    float fMilesDriven;
    ...
};
~~~~

Global variables are similar but prefixed with g and camel-capped.

<!--?prettify?-->
~~~~
bool gLoggingEnabled
~~~~

Local variables and arguments are camel-capped with no initial cap.

<!--?prettify?-->
~~~~
int herdCats(const Array& cats) {
    int numCats = cats.count();
}
~~~~

Variables declared `constexpr` or `const`, and whose value is fixed for the
duration of the program, are named with a leading "k" and then camel-capped.

<!--?prettify?-->
~~~~
int drawPicture() {
    constexpr SkISize kPictureSize = {100, 100};
    constexpr float kZoom = 1.0f;
}
~~~~

Enum values are also prefixed with k. Unscoped enum values are postfixed with
an underscore and singular name of the enum name. The enum itself should be
singular for exclusive values or plural for a bitfield. If a count is needed it
is  `k<singular enum name>Count` and not be a member of the enum (see example),
or a kLast member of the enum is fine too.

<!--?prettify?-->
~~~~
// Enum class does not need suffixes.
enum class SkPancakeType {
     kBlueberry,
     kPlain,
     kChocolateChip,
};
~~~~

<!--?prettify?-->
~~~~
// Enum should have a suffix after the enum name.
enum SkDonutType {
     kGlazed_DonutType,
     kSprinkles_DonutType,
     kChocolate_DonutType,
     kMaple_DonutType,

     kLast_DonutType = kMaple_DonutType
};

static const SkDonutType kDonutTypeCount = kLast_DonutType + 1;
~~~~

<!--?prettify?-->
~~~~
enum SkSausageIngredientBits {
    kFennel_SausageIngredientBit = 0x1,
    kBeef_SausageIngredientBit   = 0x2
};
~~~~

<!--?prettify?-->
~~~~
enum SkMatrixFlags {
    kTranslate_MatrixFlag = 0x1,
    kRotate_MatrixFlag    = 0x2
};
~~~~

Macros are all caps with underscores between words. Macros that have greater
than file scope should be prefixed SK or GR.

Static non-class functions in implementation files are lower-case with
underscores separating words:

<!--?prettify?-->
~~~~
static inline bool tastes_like_chicken(Food food) {
    return kIceCream_Food != food;
}
~~~~

Externed functions or static class functions are camel-capped with an initial cap:

<!--?prettify?-->
~~~~
bool SkIsOdd(int n);

class SkFoo {
public:
    static int FooInstanceCount();

    // Not static.
    int barBaz();
};
~~~~

Macros
------

Ganesh macros that are GL-specific should be prefixed GR_GL.

<!--?prettify?-->
~~~~
#define GR_GL_TEXTURE0 0xdeadbeef
~~~~

Ganesh prefers that macros are always defined and the use of `#if MACRO` rather than
`#ifdef MACRO`.

<!--?prettify?-->
~~~~
#define GR_GO_SLOWER 0
...
#if GR_GO_SLOWER
    Sleep(1000);
#endif
~~~~

Skia tends to use `#ifdef SK_MACRO` for boolean flags.

Braces
------

Open braces don't get a newline. `else` and `else if` appear on same line as
opening and closing braces unless preprocessor conditional compilation
interferes. Braces are always used with `if`, `else`, `while`, `for`, and `do`.

<!--?prettify?-->
~~~~
if (...) {
    oneOrManyLines;
}

if (...) {
    oneOrManyLines;
} else if (...) {
    oneOrManyLines;
} else {
    oneOrManyLines;
}

for (...) {
    oneOrManyLines;
}

while (...) {
    oneOrManyLines;
}

void function(...) {
    oneOrManyLines;
}

if (!error) {
    proceed_as_usual();
}
#if HANDLE_ERROR
else {
    freak_out();
}
#endif
~~~~

Flow Control
------------

There is a space between flow control words and parentheses and between
parentheses and braces:

<!--?prettify?-->
~~~~
while (...) {
}

do {
} while(...);

switch (...) {
...
}
~~~~

Cases and default in switch statements are indented from the switch.

<!--?prettify?-->
~~~~
switch (color) {
    case kBlue:
        ...
        break;
    case kGreen:
        ...
        break;
    ...
    default:
       ...
       break;
}
~~~~

Fallthrough from one case to the next is annotated with `[[fallthrough]]`.
However, when multiple case statements in a row are used, they do not need the
`[[fallthrough]]` annotation.

<!--?prettify?-->
~~~~
switch (recipe) {
    ...
    case kSmallCheesePizza_Recipe:
    case kLargeCheesePizza_Recipe:
        ingredients |= kCheese_Ingredient | kDough_Ingredient | kSauce_Ingredient;
        break;
    case kCheeseOmelette_Recipe:
        ingredients |= kCheese_Ingredient;
        [[fallthrough]]
    case kPlainOmelette_Recipe:
        ingredients |= (kEgg_Ingredient | kMilk_Ingredient);
        break;
    ...
}
~~~~

When a block is needed to declare variables within a case follow this pattern:

<!--?prettify?-->
~~~~
switch (filter) {
    ...
    case kGaussian_Filter: {
        Bitmap srcCopy = src->makeCopy();
        ...
        break;
    }
    ...
};
~~~~

Classes
-------

Unless there is a need for forward declaring something, class declarations
should be ordered `public`, `protected`, `private`. Each should be preceded by a
newline. Within each visibility section (`public`, `private`), fields should not be
intermixed with methods.  It's nice to keep all data fields together at the end.

<!--?prettify?-->
~~~~
class SkFoo {

public:
    ...

protected:
    ...

private:
    void barHelper(...);
    ...

    SkBar fBar;
    ...
};
~~~~

Subclasses should have a private typedef of their super class called INHERITED:

<!--?prettify?-->
~~~~
class GrDillPickle : public GrPickle {
    ...
private:
    typedef GrPickle INHERITED;
};
~~~~

Virtual functions that are overridden in derived classes should use override,
and the virtual keyword should be omitted.

<!--?prettify?-->
~~~~
void myVirtual() override {
}
~~~~

All references to base-class implementations of a virtual function
should be explicitly qualified:

<!--?prettify?-->
~~~~
void myVirtual() override {
    ...
    this->INHERITED::myVirtual();
    ...
}
~~~~

Constructor initializers should be one per line, indented, with punctuation
placed before the initializer. This is a fairly new rule so much of the existing
code is non-conforming. Please fix as you go!

<!--?prettify?-->
~~~~
GrDillPickle::GrDillPickle()
    : GrPickle()
    , fSize(kDefaultPickleSize) {
    ...
}
~~~~

Constructors that take one argument should almost always be explicit, with
exceptions made only for the (rare) automatic compatibility class.

<!--?prettify?-->
~~~~
class Foo {
    explicit Foo(int x);  // Good.
    Foo(float y);         // Spooky implicit conversion from float to Foo.  No no no!
    ...
};
~~~~

Method calls within method calls should be prefixed with dereference of the
'this' pointer. For example:

<!--?prettify?-->
~~~~
this->method();
~~~~

A common pattern for virtual methods in Skia is to include a public non-virtual
(or final) method, paired with a private virtual method named "onMethodName".
This ensures that the base-class method is always invoked and gives it control
over how the virtual method is used, rather than relying on each subclass to
call `INHERITED::onMethodName`. For example:

<!--?prettify?-->
~~~~
class SkSandwich {
public:
    void assemble() {
        // All sandwiches must have bread on the top and bottom.
        this->addIngredient(kBread_Ingredient);
        this->onAssemble();
        this->addIngredient(kBread_Ingredient);
    }
    bool cook() {
        return this->onCook();
    }

private:
    // All sandwiches must implement onAssemble.
    virtual void onAssemble() = 0;
    // Sandwiches can remain uncooked by default.
    virtual bool onCook() { return true; }
};

class SkGrilledCheese : public SkSandwich {
private:
    void onAssemble() override {
        this->addIngredient(kCheese_Ingredient);
    }
    bool onCook() override {
        return this->toastOnGriddle();
    }
};

class SkPeanutButterAndJelly : public SkSandwich {
private:
    void onAssemble() override {
        this->addIngredient(kPeanutButter_Ingredient);
        this->addIngredient(kGrapeJelly_Ingredient);
    }
};
~~~~

Integer Types
-------------

We follow the Google C++ guide for ints and are slowly making older code conform to this

(http://google-styleguide.googlecode.com/svn/trunk/cppguide.xml#Integer_Types)

Summary: Use `int` unless you have need a guarantee on the bit count, then use
`stdint.h` types (`int32_t`, etc). Assert that counts, etc are not negative instead
of using unsigned. Bitfields use `uint32_t` unless they have to be made shorter
for packing or performance reasons.

`nullptr`, 0
------------

Use `nullptr` for pointers, 0 for ints. We suggest explicit `nullptr` comparisons when
checking for `nullptr` pointers, as documentation:

<!--?prettify?-->
~~~~
if (nullptr == x) {  // slightly preferred over if (!x)
   ...
}
~~~~

When checking non-`nullptr` pointers we think implicit comparisons read better than
an explicit comparison's double negative:

<!--?prettify?-->
~~~~
if (x) {  // slightly preferred over if (nullptr != x)
   ...
}
~~~~

Function Parameters
-------------------

Mandatory constant object parameters are passed to functions as const references.
Optional constant object parameters are passed to functions as const pointers.
Mutable object parameters are passed to functions as pointers.
We very rarely pass anything by non-const reference.

<!--?prettify?-->
~~~~
// src and paint are optional
void SkCanvas::drawBitmapRect(const SkBitmap& bitmap, const SkIRect* src,
                              const SkRect& dst, const SkPaint* paint = nullptr);

// metrics is mutable (it is changed by the method)
SkScalar SkPaint::getFontMetrics(FontMetric* metrics, SkScalar scale) const;

~~~~

If function arguments or parameters do not all fit on one line, the overflowing
parameters may be lined up with the first parameter on the next line

<!--?prettify?-->
~~~~
void drawBitmapRect(const SkBitmap& bitmap, const SkRect& dst,
                    const SkPaint* paint = nullptr) {
    this->drawBitmapRectToRect(bitmap, nullptr, dst, paint,
                               kNone_DrawBitmapRectFlag);
}
~~~~

or all parameters placed on the next line and indented eight spaces

<!--?prettify?-->
~~~~
void drawBitmapRect(
        const SkBitmap& bitmap, const SkRect& dst,
        const SkPaint* paint = nullptr) {
    this->drawBitmapRectToRect(
            bitmap, nullptr, dst, paint, kNone_DrawBitmapRectFlag);
}
~~~~

Python
------

Python code follows the [Google Python Style Guide](http://google-styleguide.googlecode.com/svn/trunk/pyguide.html).

