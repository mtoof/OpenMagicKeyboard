// SPDX-License-Identifier: MIT
using System;
using System.Collections.Generic;
using System.IO;
using System.Runtime.Serialization;
using System.Runtime.Serialization.Json;
using System.Text;

namespace OpenMagicKeyboard.Gui
{
    public enum KeyTarget { Original, LeftCtrl, RightCtrl, LeftAlt, RightAlt, LeftWindows, RightWindows, Fn, F23, Disabled, CapsLock, LeftShift, RightShift, Menu }

    [DataContract]
    public sealed class KeyboardIdentity
    {
        [DataMember(Name = "vendorId", IsRequired = true)] public int VendorId;
        [DataMember(Name = "productId", IsRequired = true)] public int ProductId;
        [DataMember(Name = "interfaceNumber", IsRequired = true)] public int InterfaceNumber;
    }

    [DataContract]
    public sealed class MappingProfile
    {
        public static readonly string[] Sources = { "LeftCtrl", "RightCtrl", "LeftAlt", "RightAlt", "LeftWindows", "RightWindows", "Fn", "CapsLock", "LeftShift", "RightShift" };
        public static readonly string[] SourceLabels = { "Left Control", "Right Control", "Left Option / Alt", "Right Option / Alt", "Left Command / Windows", "Right Command / Windows", "Fn / Globe", "Caps Lock", "Left Shift", "Right Shift" };
        public static readonly string[] TargetLabels = { "Keep original key", "Left Control", "Right Control", "Left Alt / Option", "Right Alt / Option", "Left Windows / Command", "Right Windows / Command", "Fn / Globe", "F23", "Disabled", "Caps Lock", "Left Shift", "Right Shift", "Context menu" };
        /// <summary>Compact names for selectors and the keyboard drawing, indexed by KeyTarget.</summary>
        public static readonly string[] ShortLabels = { "original", "ctrl", "right ctrl", "alt", "alt gr", "win", "right win", "fn", "F23", "off", "caps", "shift", "right shift", "menu" };
        [DataMember(Name = "schemaVersion", IsRequired = true)] public int SchemaVersion;
        [DataMember(Name = "device", IsRequired = true)] public KeyboardIdentity Device;
        [DataMember(Name = "mappings", IsRequired = true)] public Dictionary<string, string> Mappings;
        [DataMember(Name = "fKeyMode")] public int FKeyMode;
        [DataMember(Name = "fnCombinations")] public bool FnCombinations;
        [DataMember(Name = "swapExchangedKeys")] public bool SwapExchangedKeys;
        [DataMember(Name = "isoLayout")] public bool IsoLayout; // drawing only; never sent to the driver
        public const int MaxBytes = 16384;

        public static MappingProfile Defaults()
        {
            var profile = new MappingProfile {
                SchemaVersion = 2,
                Device = new KeyboardIdentity { VendorId = 0x05AC, ProductId = 0x0320, InterfaceNumber = 1 },
                Mappings = new Dictionary<string, string>(StringComparer.Ordinal)
            };
            foreach (string source in Sources) profile.Mappings.Add(source, KeyTarget.Original.ToString());
            return profile;
        }
        public void Validate()
        {
            if (SchemaVersion != 2 || FKeyMode < 0 || FKeyMode > 2) throw new InvalidDataException("Unsupported profile version or F-key mode.");
            if (Device == null || Device.VendorId != 0x05AC || Device.ProductId != 0x0320 || Device.InterfaceNumber != 1)
                throw new InvalidDataException("This profile must target only USB VID_05AC/PID_0320/MI_01.");
            if (Mappings == null || Mappings.Count != Sources.Length)
                throw new InvalidDataException("A profile must contain exactly the ten supported source keys.");
            foreach (string source in Sources)
            {
                string value;
                KeyTarget target;
                if (!Mappings.TryGetValue(source, out value) || !Enum.TryParse<KeyTarget>(value, false, out target) ||
                    !Enum.IsDefined(typeof(KeyTarget), target) || target.ToString() != value)
                    throw new InvalidDataException("Invalid mapping for " + source + ".");
            }
        }
        public KeyTarget Get(string source) { Validate(); return (KeyTarget)Enum.Parse(typeof(KeyTarget), Mappings[source]); }
        public void Set(string source, KeyTarget target)
        {
            if (Array.IndexOf(Sources, source) < 0 || !Enum.IsDefined(typeof(KeyTarget), target))
                throw new InvalidDataException("Unsupported mapping.");
            Mappings[source] = target.ToString();
        }
        private KeyTarget Effective(string source)
        {
            KeyTarget target = Get(source);
            return target == KeyTarget.Original ? (KeyTarget)Enum.Parse(typeof(KeyTarget), source) : target;
        }
        private void SwapPair(string a, string b)
        {
            KeyTarget first = Effective(a), second = Effective(b);
            Set(a, second.ToString() == a ? KeyTarget.Original : second);
            Set(b, first.ToString() == b ? KeyTarget.Original : first);
        }
        public void SwapControlsAndWindows() { SwapPair("LeftCtrl", "LeftWindows"); SwapPair("RightCtrl", "RightWindows"); }
        public void SwapAltAndWindows() { SwapPair("LeftAlt", "LeftWindows"); SwapPair("RightAlt", "RightWindows"); }
        private static DataContractJsonSerializer Serializer()
        {
            return new DataContractJsonSerializer(typeof(MappingProfile), new DataContractJsonSerializerSettings {
                UseSimpleDictionaryFormat = true, MaxItemsInObjectGraph = 128
            });
        }
        public byte[] Encode()
        {
            Validate();
            using (var stream = new MemoryStream()) { Serializer().WriteObject(stream, this); return stream.ToArray(); }
        }
        public static MappingProfile Decode(byte[] bytes)
        {
            if (bytes == null || bytes.Length == 0 || bytes.Length > MaxBytes) throw new InvalidDataException("Profile is empty or too large.");
            using (var stream = new MemoryStream(bytes, false))
            {
                var profile = (MappingProfile)Serializer().ReadObject(stream);
                if (profile == null) throw new InvalidDataException("Profile is missing.");
                if (profile.SchemaVersion == 1 && profile.Mappings != null && profile.Mappings.Count == 7) {
                    for (int i = 7; i < Sources.Length; ++i) profile.Mappings.Add(Sources[i], "Original");
                    profile.SchemaVersion = 2;
                }
                profile.Validate();
                return profile;
            }
        }
        public static MappingProfile Load(string path)
        {
            using (var stream = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.Read))
            {
                if (stream.Length > MaxBytes) throw new InvalidDataException("Profile is too large.");
                var bytes = new byte[checked((int)stream.Length)];
                int position = 0, count;
                while (position < bytes.Length && (count = stream.Read(bytes, position, bytes.Length - position)) > 0) position += count;
                if (position != bytes.Length) throw new InvalidDataException("Incomplete profile read.");
                return Decode(bytes);
            }
        }
        public void Save(string path)
        {
            byte[] bytes = Encode();
            string absolute = Path.GetFullPath(path);
            string parent = Path.GetDirectoryName(absolute);
            Directory.CreateDirectory(parent);
            string temporary = Path.Combine(parent, ".profile-" + Guid.NewGuid().ToString("N") + ".tmp");
            bool created = false;
            try
            {
                using (var stream = new FileStream(temporary, FileMode.CreateNew, FileAccess.Write, FileShare.None))
                {
                    created = true;
                    stream.Write(bytes, 0, bytes.Length);
                    stream.Flush(true);
                }
                if (File.Exists(absolute)) File.Replace(temporary, absolute, null);
                else File.Move(temporary, absolute);
                created = false;
            }
            finally { if (created && File.Exists(temporary)) File.Delete(temporary); }
        }
    }
}
