// SPDX-License-Identifier: MIT. Synthetic reports only; never opens a keyboard.
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <vector>
#include <memory>
extern "C" {
#include "../../driver/ReportCore.h"
#include "../../driver/NativeCore.h"
}
static unsigned checks;
static void Check(bool yes, const char *name) { if (!yes) { std::fprintf(stderr, "FAIL: %s\n", name); std::exit(1); } ++checks; }
// Synthetic boot-style keyboard plus a vendor Fn bit. This is NOT the device descriptor.
static const unsigned char descriptor[] = {
    0x05,1,0x09,6,0xa1,1,0x85,1,
    0x05,7,0x19,0xe0,0x29,0xe7,0x15,0,0x25,1,0x75,1,0x95,8,0x81,2,
    0x05,0xff,0x09,3,0x95,1,0x81,2,0x95,7,0x81,1,
    0x05,7,0x19,0,0x29,0xff,0x15,0,0x26,0xff,0,0x75,8,0x95,6,0x81,0,0xc0
};
static unsigned seed = 0x35ca55u;
static unsigned Random() { seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5; return seed; }
static std::vector<unsigned char> Sequence(const std::vector<unsigned char>& data) {
    std::vector<unsigned char> result;
    if(data.size()<=255)result={0x35,static_cast<unsigned char>(data.size())};
    else result={0x36,static_cast<unsigned char>(data.size()>>8),static_cast<unsigned char>(data.size())};
    result.insert(result.end(),data.begin(),data.end()); return result;
}
// Device configuration captured from our keyboard; no input reports are retained.
static const unsigned char bluetoothDescriptor[] = {
    0x05,0x01,0x09,0x06,0xa1,0x01,0x85,0x01,0x05,0x07,0x15,0x00,0x25,0x01,0x19,0xe0,
    0x29,0xe7,0x75,0x01,0x95,0x08,0x81,0x02,0x95,0x05,0x75,0x01,0x05,0x08,0x19,0x01,
    0x29,0x05,0x91,0x02,0x95,0x01,0x75,0x03,0x91,0x03,0x95,0x08,0x75,0x01,0x15,0x00,
    0x25,0x01,0x06,0x00,0xff,0x09,0x03,0x81,0x03,0x95,0x06,0x75,0x08,0x15,0x00,0x25,
    0x65,0x05,0x07,0x19,0x00,0x29,0x65,0x81,0x00,0x95,0x01,0x75,0x01,0x15,0x00,0x25,
    0x01,0x05,0x0c,0x09,0x00,0x81,0x01,0x95,0x01,0x75,0x01,0x06,0x01,0xff,0x09,0x03,
    0x81,0x02,0x05,0x0c,0x09,0x00,0x95,0x01,0x75,0x01,0x81,0x01,0x0a,0x9e,0x01,0x95,
    0x01,0x75,0x01,0x81,0x02,0x95,0x01,0x75,0x04,0x81,0x03,0x06,0x02,0xff,0x09,0x55,
    0x85,0x55,0x15,0x00,0x26,0xff,0x00,0x75,0x08,0x95,0x40,0xb1,0xa2,0xc0,0x06,0x00,
    0xff,0x09,0x14,0xa1,0x01,0x85,0x90,0x05,0x84,0x75,0x01,0x95,0x03,0x15,0x00,0x25,
    0x01,0x09,0x61,0x05,0x85,0x09,0x44,0x09,0x46,0x81,0x02,0x95,0x05,0x81,0x01,0x75,
    0x08,0x95,0x01,0x15,0x00,0x26,0xff,0x00,0x09,0x65,0x81,0x02,0xc0
};
struct GlobalState { unsigned value[10]{}, width[10]{}, known{}; };
struct SemanticItem { unsigned prefix, value; GlobalState globals; };
// Independently compare each local/main item and its complete active globals,
// including widths, to catch changed battery/feature/output semantics too.
static std::vector<SemanticItem> Semantics(const unsigned char* bytes, unsigned length) {
    GlobalState globals; std::vector<GlobalState> stack; std::vector<SemanticItem> items;
    for(unsigned pos=0;pos<length;) {
        unsigned prefix=bytes[pos++], size=prefix&3, type=(prefix>>2)&3, tag=prefix>>4, value=0;
        if(size==3)size=4;
        Check(prefix!=0xfe && size<=length-pos,"semantic fixture item bounds");
        for(unsigned i=0;i<size;++i)value|=unsigned(bytes[pos+i])<<(8*i); pos+=size;
        if(type==1 && tag<10) { globals.value[tag]=value;globals.width[tag]=size;globals.known|=1u<<tag; }
        else if(type==1 && tag==10)stack.push_back(globals);
        else if(type==1 && tag==11) { Check(!stack.empty(),"semantic global stack");globals=stack.back();stack.pop_back(); }
        else items.push_back({prefix,value,globals});
    }
    Check(stack.empty(),"semantic stack balanced");return items;
}
static void SameSemantics(const unsigned char* expected,unsigned expectedSize,const unsigned char* actual,unsigned actualSize,bool extraCollection) {
    auto before=Semantics(expected,expectedSize),after=Semantics(actual,actualSize);
    Check(extraCollection?after.size()>before.size():after.size()==before.size(),"main/local item counts preserved");
    for(size_t i=0;i<before.size();++i) {
        Check(before[i].prefix==after[i].prefix && before[i].value==after[i].value && before[i].globals.known==after[i].globals.known &&
            !std::memcmp(before[i].globals.value,after[i].globals.value,sizeof(GlobalState::value)) &&
            !std::memcmp(before[i].globals.width,after[i].globals.width,sizeof(GlobalState::width)),"all original main/local semantics preserved");
    }
}
static void CompactBluetoothChecks() {
    unsigned char out[MK_SDP_MAX]{}; MK_NATIVE_RESULT native{}; MK_LAYOUT original{};
    auto patched=std::vector<unsigned char>(std::begin(bluetoothDescriptor),std::end(bluetoothDescriptor));
    Check(patched.size()==189 && patched[0x3f]==0x25 && patched[0x45]==0x29,"captured descriptor expansion locations");
    patched[0x40]=patched[0x46]=0x7f;
    Check(MkParseBluetoothDescriptor(bluetoothDescriptor,sizeof(bluetoothDescriptor),&original)!=0,"captured Bluetooth layout");
    Check(MkNativeDescriptor(bluetoothDescriptor,sizeof(bluetoothDescriptor),out,sizeof(out),1,0,0,&native)!=0 &&
        native.Length==sizeof(bluetoothDescriptor) && !std::memcmp(out,bluetoothDescriptor,sizeof(bluetoothDescriptor)),"raw Bluetooth pass-through exact");
    Check(MkNativeDescriptor(bluetoothDescriptor,sizeof(bluetoothDescriptor),out,sizeof(out),1,1,0,&native)!=0 && native.Length<sizeof(bluetoothDescriptor),"Bluetooth opt-in compacts redundant globals");
    SameSemantics(patched.data(),static_cast<unsigned>(patched.size()),out,native.Length,false);
    Check(MkNativeDescriptor(bluetoothDescriptor,sizeof(bluetoothDescriptor),out,sizeof(bluetoothDescriptor),1,1,1,&native)!=0 &&
        native.Length<=sizeof(bluetoothDescriptor) && native.ReportId==2 && native.Layout.ArrayMax==127,"native Bluetooth fits original descriptor buffer");
    Check(native.Layout.Bytes==original.Bytes && native.Layout.ReportId==original.ReportId && native.Layout.Fn==original.Fn &&
        native.Layout.ArrayBit==original.ArrayBit && native.Layout.ArrayCount==original.ArrayCount &&
        !std::memcmp(native.Layout.Modifier,original.Modifier,sizeof(original.Modifier)),"keyboard wire layout preserved");
    SameSemantics(patched.data(),static_cast<unsigned>(patched.size()),out,native.Length,true);
    // Width changes are deliberately retained; Pop restores the known Page.
    const unsigned char prefix[]={5,1,0xa4,5,7,5,7,0xb4,5,1,0x75,1,0x76,1,0};
    const unsigned char compactPrefix[]={5,1,0xa4,5,7,0xb4,0x75,1,0x76,1,0};
    auto stacked=std::vector<unsigned char>(std::begin(prefix),std::end(prefix));stacked.insert(stacked.end(),patched.begin(),patched.end());
    Check(MkNativeDescriptor(stacked.data(),static_cast<unsigned>(stacked.size()),out,sizeof(out),1,1,0,&native)!=0 &&
        !std::memcmp(out,compactPrefix,sizeof(compactPrefix)),"global Push/Pop and item widths preserved");
    SameSemantics(stacked.data(),static_cast<unsigned>(stacked.size()),out,native.Length,false);
    std::vector<unsigned char> entry={8,0x22,0x25,static_cast<unsigned char>(sizeof(bluetoothDescriptor))};
    entry.insert(entry.end(),std::begin(bluetoothDescriptor),std::end(bluetoothDescriptor));
    auto list=Sequence(Sequence(entry));std::vector<unsigned char> attributes={9,2,6};attributes.insert(attributes.end(),list.begin(),list.end());
    auto stream=Sequence(Sequence(attributes));
    Check(MkSdpDescriptor(stream.data(),static_cast<unsigned>(stream.size()),out,static_cast<unsigned>(stream.size()),0,0,&native)!=0 &&
        native.Length==stream.size() && !std::memcmp(out,stream.data(),stream.size()),"original-size SDP pass-through exact");
    Check(MkSdpDescriptor(stream.data(),static_cast<unsigned>(stream.size()),out,static_cast<unsigned>(stream.size()),1,1,&native)!=0 &&
        native.Length<=stream.size() && native.DescriptorLength<=sizeof(bluetoothDescriptor) && out[native.DescriptorOffset-2]==0x25,"native SDP fits original caller buffer and keeps text width");
    SameSemantics(patched.data(),static_cast<unsigned>(patched.size()),out+native.DescriptorOffset,native.DescriptorLength,true);
    entry[2]=0x26;entry.insert(entry.begin()+3,0);
    list=Sequence(Sequence(entry));attributes={9,2,6};attributes.insert(attributes.end(),list.begin(),list.end());stream=Sequence(attributes);
    Check(MkSdpDescriptor(stream.data(),static_cast<unsigned>(stream.size()),out,sizeof(out),1,1,&native)!=0 &&
        out[native.DescriptorOffset-3]==0x26,"existing 16-bit SDP text width retained");
    Check(MkNativeDescriptor(descriptor,sizeof(descriptor),out,sizeof(out),0,0,1,&native)!=0 &&
        native.Length==sizeof(descriptor)+MK_NATIVE_EXTRA,"USB descriptor remains uncompressed");
}
static void NativeChecks() {
    const unsigned char bt[]={5,1,9,6,0xa1,1,0x85,1,5,7,0x19,0xe0,0x29,0xe7,0x15,0,0x25,1,0x75,1,0x95,8,0x81,2,
        0x75,8,0x95,1,0x81,3,0x19,0,0x29,0x65,0x15,0,0x25,0x65,0x75,8,0x95,6,0x81,0,
        6,1,0xff,9,3,0x15,0,0x25,1,0x75,1,0x95,1,0x81,2,0x95,7,0x81,3,0xc0};
    MK_LAYOUT layout{}; MK_NATIVE_RESULT native{}; unsigned char out[MK_SDP_MAX]{};
    Check(MkParseBluetoothDescriptor(bt,sizeof(bt),&layout)!=0 && layout.ArrayMax==101 && layout.Fn==64,"Bluetooth Fn page and array range");
    unsigned char report[10]={1}; report[9]=1; MK_POLICY policy{}; policy.Enabled=policy.FnVerified=1; policy.Map[6]=8;
    Check(!MkTransform(&layout,&policy,report,sizeof(report)) && report[9]==1,"unadvertised Bluetooth F23 fails open");
    Check(MkNativeDescriptor(bt,sizeof(bt),out,sizeof(out),1,1,1,&native)!=0,"Bluetooth/native descriptor build");
    Check(native.Length>sizeof(bt) && native.Length<=sizeof(bt)+MK_NATIVE_EXTRA && native.Layout.ArrayMax==127 && native.ReportId!=1 && native.ReportId!=0,"native length, unique ID and F23 capability");
    Check(MkTransform(&native.Layout,&policy,report,sizeof(report))!=0 && report[3]==0x72,"Bluetooth F23 after advertised extension");
    Check(!MkNativeDescriptor(bt,sizeof(bt),out,sizeof(bt),1,1,1,&native),"native descriptor refuses insufficient capacity");
    std::vector<unsigned char> entry={8,0x22,0x25,static_cast<unsigned char>(sizeof(bt))}; entry.insert(entry.end(),std::begin(bt),std::end(bt));
    auto list=Sequence(Sequence(entry)); std::vector<unsigned char> attributes={9,2,6}; attributes.insert(attributes.end(),list.begin(),list.end());
    // A later sequence must not corrupt the already located descriptor offset.
    const unsigned char later[]={9,1,0,0x35,2,8,0}; attributes.insert(attributes.end(),std::begin(later),std::end(later));
    auto stream=Sequence(Sequence(attributes));
    auto largeAttributes=attributes; largeAttributes.insert(largeAttributes.end(),{9,1,1,0x25,180}); largeAttributes.insert(largeAttributes.end(),180,'X');
    auto largeStream=Sequence(Sequence(largeAttributes));
    Check(MkSdpDescriptor(largeStream.data(),static_cast<unsigned>(largeStream.size()),out,sizeof(out),1,1,&native)!=0 && MkParseBluetoothDescriptor(out+native.DescriptorOffset,native.DescriptorLength,&layout)!=0,"SDP 16-bit container lengths");
    auto extendedId=std::vector<unsigned char>(std::begin(bt),std::end(bt));
    extendedId.insert(extendedId.end(),{5,0xff,9,20,0xa1,1,0x86,2,0,0x15,0,0x26,0xff,0,0x75,8,0x95,1,0xb1,2,0xc0});
    Check(MkNativeDescriptor(extendedId.data(),static_cast<unsigned>(extendedId.size()),out,sizeof(out),1,1,1,&native)!=0 && native.ReportId==3,"native ID avoids multi-byte report-ID declarations");
    auto growing=std::vector<unsigned char>(std::begin(bt),std::end(bt));
    growing.insert(growing.end(),{6,2,0xff,9,0x55,0xa1,1,0x85,0x55,0x15,0,0x26,0xff,0,0x75,8,0x95,1});
    while(growing.size()+5<=250)growing.insert(growing.end(),{9,1,0xb1,2});growing.push_back(0xc0);
    std::vector<unsigned char> growingEntry={8,0x22,0x25,static_cast<unsigned char>(growing.size())};growingEntry.insert(growingEntry.end(),growing.begin(),growing.end());
    auto growingList=Sequence(Sequence(growingEntry));std::vector<unsigned char> growingAttributes={9,2,6};growingAttributes.insert(growingAttributes.end(),growingList.begin(),growingList.end());
    auto growingStream=Sequence(growingAttributes);
    Check(MkSdpDescriptor(growingStream.data(),static_cast<unsigned>(growingStream.size()),out,sizeof(out),1,1,&native)!=0 &&
        native.DescriptorLength>255 && out[native.DescriptorOffset-3]==0x26 &&
        MkParseBluetoothDescriptor(out+native.DescriptorOffset,native.DescriptorLength,&layout)!=0,"SDP text and container headers grow safely when required");
    Check(MkSdpDescriptor(stream.data(),static_cast<unsigned>(stream.size()),out,sizeof(out),0,0,&native)!=0 && native.Length==stream.size() && !std::memcmp(stream.data(),out,stream.size()),"Bluetooth pass-through descriptor unchanged");
    Check(MkSdpDescriptor(stream.data(),static_cast<unsigned>(stream.size()),out,sizeof(out),1,1,&native)!=0,"SDP descriptor transform");
    Check(native.DescriptorOffset+native.DescriptorLength<=native.Length && MkParseBluetoothDescriptor(out+native.DescriptorOffset,native.DescriptorLength,&layout)!=0 && layout.ArrayMax==127,"SDP nested lengths and descriptor offset");
    Check(!MkSdpDescriptor(stream.data(),static_cast<unsigned>(stream.size()-1),out,sizeof(out),1,1,&native),"truncated SDP fails open");
    MkParseBluetoothDescriptor(bt,sizeof(bt),&layout); policy={}; policy.Enabled=policy.FnVerified=policy.NativeReady=1; policy.FKeyMode=2;
    MK_STATE state{}; MK_EFFECTS effects{}; unsigned char key[10]={1,0,0,0x3a};
    Check(MkTransformState(&layout,&policy,&state,&effects,key,10)!=0 && key[3]==0 && effects.Count==1 && effects.Events[0].Action==1,"native brightness without helper");
    key[3]=0; Check(MkTransformState(&layout,&policy,&state,&effects,key,10)!=0 && effects.Count==1 && !effects.Events[0].Down,"native brightness matched release");
    state={}; key[3]=0x41; Check(MkTransformState(&layout,&policy,&state,&effects,key,10)!=0 && key[3]==0x41 && effects.Count==0,"other icons preserve F-keys without helper");
    unsigned char fuzz[512];
    struct Guard { unsigned before; unsigned char bytes[MK_SDP_MAX]; unsigned after; };
    auto guarded=std::make_unique<Guard>(); guarded->before=0x12345678; guarded->after=0x87654321;
    for(unsigned pass=0;pass<10000;++pass) {
        const unsigned length=Random()%sizeof(fuzz); for(unsigned i=0;i<length;++i)fuzz[i]=static_cast<unsigned char>(Random());
        (void)MkSdpDescriptor(fuzz,length,guarded->bytes,sizeof(guarded->bytes),1,1,&native);
        Check(guarded->before==0x12345678 && guarded->after==0x87654321,"SDP output canaries");
    }
}
int main(int argc, char **argv) {
    if (argc == 3 && !std::strcmp(argv[1], "--descriptor")) {
        std::ifstream file(argv[2], std::ios::binary);
        if (!file) { std::fprintf(stderr, "Cannot open descriptor file.\n"); return 2; }
        unsigned char bytes[MK_MAX_DESCRIPTOR + 1]{};
        file.read(reinterpret_cast<char *>(bytes), sizeof(bytes));
        const auto length = file.gcount();
        if (file.bad() || length <= 0 || length > MK_MAX_DESCRIPTOR) {
            std::fprintf(stderr, "Descriptor must contain 1..4096 bytes.\n"); return 2;
        }
        MK_LAYOUT layout{};
        int valid = MkParseDescriptor(bytes, static_cast<unsigned>(length), &layout);
        if(!valid || layout.Fn==MK_NONE) { MK_LAYOUT bluetooth{}; if(MkParseBluetoothDescriptor(bytes,static_cast<unsigned>(length),&bluetooth) && bluetooth.Fn!=MK_NONE) {layout=bluetooth;valid=1;} }
        std::printf("DescriptorBytes=%u; LayoutValid=%d; ReportId=%u; ReportBytes=%u; FnBit=%u; ArrayBit=%u; ArrayCount=%u\n",
            static_cast<unsigned>(length), valid, static_cast<unsigned>(layout.ReportId),
            static_cast<unsigned>(layout.Bytes), static_cast<unsigned>(layout.Fn),
            static_cast<unsigned>(layout.ArrayBit), static_cast<unsigned>(layout.ArrayCount));
        return valid ? 0 : 2;
    }
    if (argc != 1) { std::fprintf(stderr, "Usage: MagicKeyboardDriverTests.exe [--descriptor FILE]\n"); return 2; }
    MK_LAYOUT l{}; MK_POLICY p{};
    CompactBluetoothChecks(); NativeChecks();
    Check(MkParseDescriptor(descriptor, sizeof(descriptor), &l) != 0, "synthetic layout");
    Check(l.ReportId == 1 && l.Bytes == 9 && l.Fn == 8 && l.ArrayBit == 16 && l.ArrayCount == 6, "derived locations");
    unsigned char b[9] = {1,1,0,4,0,0,0,0,0}, copy[9];
    std::memcpy(copy,b,9); Check(!MkTransform(&l,&p,b,9) && !std::memcmp(copy,b,9), "default passthrough");
    p.Enabled = 1; p.Map[0] = 5; p.Map[4] = 1;
    Check(MkTransform(&l,&p,b,9) && b[1] == 8 && b[3] == 4, "Ctrl Windows swap retains ordinary key");
    b[1] = 9; Check(MkTransform(&l,&p,b,9) && b[1] == 9, "simultaneous swapped keys");
    p.Map[0] = 3; p.Map[4] = 3; b[1] = 9;
    Check(MkTransform(&l,&p,b,9) && b[1] == 4, "many to one ownership");
    b[1] = 8; Check(MkTransform(&l,&p,b,9) && b[1] == 4, "one owner release retains other");
    b[1] = 0; Check(MkTransform(&l,&p,b,9) && b[1] == 0, "all owner release");
    b[1] = 0x22; Check(MkTransform(&l,&p,b,9) && b[1] == 0x22, "both Shift keys untouched");
    p = {}; p.Enabled = p.FnVerified = 1; p.Map[6] = 8; b[1] = 0; b[2] = 1;
    Check(MkFnPressed(&l,b,9) == 1, "Fn diagnostic");
    Check(MkTransform(&l,&p,b,9) && b[2] == 0 && b[3] == 4 && b[4] == 0x72, "Fn F23 preserves ordinary key");
    unsigned char release[9] = {1,0,0,4};
    Check(MkTransform(&l,&p,release,9) && release[4] == 0, "Fn release has no F23");
    p.FnVerified = 0; b[2] = 1; std::memcpy(copy,b,9);
    Check(!MkTransform(&l,&p,b,9) && !std::memcmp(copy,b,9), "unverified Fn mapping fails open");
    p.FnVerified = 1; for(unsigned i=3;i<9;++i)b[i]=static_cast<unsigned char>(i+1);
    std::memcpy(copy,b,9); Check(!MkTransform(&l,&p,b,9) && !std::memcmp(copy,b,9), "full array never displaces typing");
    b[3] = 1; std::memcpy(copy,b,9); Check(!MkTransform(&l,&p,b,9) && !std::memcmp(copy,b,9), "rollover unchanged");
    b[0] = 0x52; std::memcpy(copy,b,9); Check(!MkTransform(&l,&p,b,9) && !std::memcmp(copy,b,9), "consumer report untouched");
    Check(!MkTransform(&l,&p,b,8) && MkFnPressed(&l,b,8)==-1, "short report unchanged");
    const unsigned sourceBits[6]={0,4,2,6,3,7}, targetBits[6]={0,4,2,6,3,7};
    for(unsigned sourceIndex=0;sourceIndex<7;++sourceIndex) for(unsigned action=0;action<10;++action) {
        unsigned char report[9]={1,0x22,0,4};
        if(sourceIndex<6)report[1]|=static_cast<unsigned char>(1u<<sourceBits[sourceIndex]);else report[2]=1;
        p={};p.Enabled=p.FnVerified=1;p.Map[sourceIndex]=static_cast<unsigned char>(action);
        Check(MkTransform(&l,&p,report,9)!=0,"every source/action transforms");
        unsigned expected=0x22;
        if(action==0 && sourceIndex<6)expected|=1u<<sourceBits[sourceIndex];
        else if(action>=1 && action<=6)expected|=1u<<targetBits[action-1];
        Check(report[1]==expected && report[3]==4,"source/action preserves Shift and typing");
        Check(report[2]==(sourceIndex==6 && action==0?1:0),"Fn source ownership");
        Check(report[4]==(action==7 || action==8?0x72:0),"logical Fn and F23 target");
    }
    const unsigned char comboIn[5]={0x2a,0x50,0x4f,0x52,0x51}, comboOut[5]={0x4c,0x4a,0x4d,0x4b,0x4e};
    for (unsigned c=0;c<5;++c) {
        MK_STATE state{}; MK_EFFECTS effects{}; p={};p.Enabled=p.FnVerified=p.FnCombinations=1;p.Map[6]=8;
        unsigned char combo[9]={1,0x22,1,comboIn[c],4};
        Check(MkTransformState(&l,&p,&state,&effects,combo,9) && combo[3]==comboOut[c] && combo[4]==4 && combo[1]==0x22,"Fn combination preserves Shift and ordinary key");
        unsigned char fnFirst[9]={1,0x22,0,comboIn[c],4};
        Check(MkTransformState(&l,&p,&state,&effects,fnFirst,9) && fnFirst[3]==comboOut[c],"Fn released first retains converted key until its release");
        unsigned char allUp[9]={1};Check(MkTransformState(&l,&p,&state,&effects,allUp,9)&&!state.ComboHeld,"combination release clears ownership");
        unsigned char plain[9]={1,0,0,comboIn[c]};Check(MkTransformState(&l,&p,&state,&effects,plain,9)&&plain[3]==comboIn[c],"plain key unchanged after combo release");
    }
    p={};p.Enabled=p.FnVerified=1;p.Map[7]=1;
    unsigned char capsReport[9]={1,0,0,0x39,4};Check(MkTransform(&l,&p,capsReport,9)&&capsReport[1]==1&&capsReport[3]==0&&capsReport[4]==4,"Caps to Ctrl removes only Caps usage");
    p={};p.Enabled=p.FnVerified=1;p.Map[8]=10;
    unsigned char shiftReport[9]={1,2,0,4};Check(MkTransform(&l,&p,shiftReport,9)&&shiftReport[1]==0&&shiftReport[4]==0x39,"Shift to Caps uses a spare array slot");
    p={};p.Enabled=p.FnVerified=p.MediaReady=1;p.FKeyMode=2;
    MK_STATE mediaState{};MK_EFFECTS mediaEffects{};unsigned char mediaReport[9]={1,0,0,0x41,4};
    Check(MkTransformState(&l,&p,&mediaState,&mediaEffects,mediaReport,9)&&mediaReport[3]==0&&mediaReport[4]==4&&mediaEffects.Count==1&&mediaEffects.Events[0].Action==8,"icon F8 removes only F8 and emits play action");
    unsigned char mediaRelease[9]={1};Check(MkTransformState(&l,&p,&mediaState,&mediaEffects,mediaRelease,9)&&mediaEffects.Count==1&&!mediaEffects.Events[0].Down,"media release paired");
    p.MediaReady=0;unsigned char noHelper[9]={1,0,0,0x41};Check(MkTransformState(&l,&p,&mediaState,&mediaEffects,noHelper,9)&&noHelper[3]==0x41&&mediaEffects.Count==0,"missing helper preserves F-key input");
    Check(!MkParseDescriptor(nullptr,1,&l) && !l.Valid, "null descriptor rejected");
    Check(!MkParseDescriptor(descriptor,MK_MAX_DESCRIPTOR+1,&l), "oversized descriptor rejected before access");
    unsigned char changed[sizeof(descriptor)]; std::memcpy(changed,descriptor,sizeof(changed)); changed[23]=6;
    Check(!MkParseDescriptor(changed,sizeof(changed),&l),"relative modifiers rejected");
    changed[23]=0x82; Check(!MkParseDescriptor(changed,sizeof(changed),&l),"reserved input flags rejected");
    for(unsigned n=0;n<sizeof(descriptor);++n) Check(!MkParseDescriptor(descriptor,n,&l), "every truncated descriptor rejected");
    unsigned char fuzz[MK_MAX_DESCRIPTOR];
    for(unsigned pass=0;pass<100000;++pass) {
        unsigned n = pass & 1 ? static_cast<unsigned>(sizeof(descriptor)) : Random()%MK_MAX_DESCRIPTOR;
        if(pass & 1) { std::memcpy(fuzz,descriptor,n); fuzz[Random()%n] = static_cast<unsigned char>(Random()); }
        else for(unsigned i=0;i<n;++i)fuzz[i]=static_cast<unsigned char>(Random());
        struct Guard { unsigned before; MK_LAYOUT layout; unsigned after; } guard{0x12345678,{},0x87654321};
        int parsed=MkParseDescriptor(fuzz,n,&guard.layout);
        Check(guard.before==0x12345678 && guard.after==0x87654321,"parser output canaries");
        if(parsed) {
            l=guard.layout;
            Check(l.Valid && l.Bytes<=MK_MAX_REPORT && l.ArrayBit+8*l.ArrayCount<=8*(l.Bytes-(l.ReportId?1:0)),"parsed array bounds");
            for(unsigned i=0;i<8;++i)Check(l.Modifier[i]<8*(l.Bytes-(l.ReportId?1:0)),"parsed modifier bounds");
            struct Report { unsigned before; unsigned char bytes[MK_MAX_REPORT]; unsigned after; } r{0x12345678,{},0x87654321};
            for(unsigned i=0;i<l.Bytes;++i)r.bytes[i]=static_cast<unsigned char>(Random());
            r.bytes[0]=l.ReportId; p={};p.Enabled=p.FnVerified=1;
            for(auto &mapping:p.Map)mapping=static_cast<unsigned char>(Random()%10);
            (void)MkTransform(&l,&p,r.bytes,l.Bytes);
            Check(r.before==0x12345678 && r.after==0x87654321,"transform canaries");
        }
    }
    std::printf("%u checks passed; 100000 malformed/mutated descriptor cases; synthetic data only.\n",checks);
    return 0;
}
