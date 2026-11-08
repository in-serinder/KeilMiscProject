using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using Capsa.Models;

namespace Capsa.Diagnostics
{
    /// <summary>
    /// 纯逻辑自检（不碰串口）：文件名 trailer 的打包 / 还原。
    /// 用法：<c>Capsa.exe --self-test [log.txt]</c>
    /// </summary>
    internal static class SelfTest
    {
        public static int Run(string? logPath)
        {
            Encoding.RegisterProvider(CodePagesEncodingProvider.Instance);

            var lines = new List<string>();
            var failed = 0;

            void Check(string name, bool ok, string? detail = null)
            {
                if (!ok)
                {
                    failed++;
                }

                lines.Add($"{(ok ? "PASS" : "FAIL")}  {name}{(detail is null ? string.Empty : $"  | {detail}")}");
            }

            // 1) 带文件名的往返
            var data = Enumerable.Range(0, 1000).Select(i => (byte)(i & 0xFF)).ToArray();
            var wrapped = FilePayloadCodec.Wrap(data, "logo.PNG");

            Check("wrap adds name + 5-byte trailer", wrapped.Length == data.Length + 5 + "logo.PNG".Length,
                $"{data.Length} -> {wrapped.Length}");
            Check("magic sits at the end",
                wrapped[^4] == (byte)'C' && wrapped[^3] == (byte)'S' && wrapped[^2] == (byte)'N' && wrapped[^1] == (byte)'X');
            Check("name length byte", wrapped[^5] == "logo.PNG".Length, wrapped[^5].ToString());

            var back = FilePayloadCodec.Unwrap(wrapped);
            Check("file name restored", back.FileName == "logo.PNG", back.FileName ?? "<null>");
            Check("extension derived", back.Extension == "png", back.Extension ?? "<null>");
            Check("payload intact", back.Data.SequenceEqual(data), $"{back.Data.Length} B");

            // 2) 无文件名
            var noName = FilePayloadCodec.Wrap(data, null);
            Check("no-name wrap length", noName.Length == data.Length + 5, noName.Length.ToString());
            var noNameBack = FilePayloadCodec.Unwrap(noName);
            Check("no-name unwrap", noNameBack.FileName is null && noNameBack.Data.SequenceEqual(data));

            // 3) 原始数据（无 trailer）必须原样返回
            var raw = FilePayloadCodec.Unwrap(data);
            Check("raw bytes untouched", raw.FileName is null && raw.Data.SequenceEqual(data));

            // 4) 太短 / 伪造魔数
            Check("short input safe", FilePayloadCodec.Unwrap(new byte[2]).FileName is null);

            var fake = new byte[] { 1, 2, 3, (byte)'C', (byte)'S', (byte)'N', (byte)'X' };
            Check("bogus length rejected", FilePayloadCodec.Unwrap(fake).FileName is null);

            // 5) 文件名规整
            Check("path stripped", FilePayloadCodec.BuildStoredName(@"C:\tmp\a.bin") == "a.bin");
            Check("invalid chars replaced", FilePayloadCodec.BuildStoredName("a<b>.bin") == "a_b_.bin");

            var longName = new string('x', 65) + ".png";
            Check("long name falls back to dump.ext", FilePayloadCodec.BuildStoredName(longName) == "dump.png",
                FilePayloadCodec.BuildStoredName(longName));
            Check("64-byte name kept", FilePayloadCodec.BuildStoredName(new string('y', 64)) == new string('y', 64));
            Check("empty name", FilePayloadCodec.BuildStoredName("   ") == string.Empty);

            // 6) 中文文件名（UTF-8 多字节）
            var cn = FilePayloadCodec.Wrap(data, "测试文件.dat");
            var cnBack = FilePayloadCodec.Unwrap(cn);
            Check("utf8 name restored", cnBack.FileName == "测试文件.dat", cnBack.FileName ?? "<null>");
            Check("utf8 extension", cnBack.Extension == "dat", cnBack.Extension ?? "<null>");

            // 7) 兼容旧版只存扩展名的 trailer（"CSFX"）
            var legacy = new byte[data.Length + 8];
            data.CopyTo(legacy, 0);
            Encoding.ASCII.GetBytes("png").CopyTo(legacy, data.Length);
            legacy[data.Length + 3] = 3;
            legacy[data.Length + 4] = (byte)'C';
            legacy[data.Length + 5] = (byte)'S';
            legacy[data.Length + 6] = (byte)'F';
            legacy[data.Length + 7] = (byte)'X';

            var legacyBack = FilePayloadCodec.Unwrap(legacy);
            Check("legacy ext trailer", legacyBack.FileName == "dump.png" && legacyBack.Data.SequenceEqual(data),
                legacyBack.FileName ?? "<null>");

            // 8) 空数据也能往返（只剩 trailer）
            var emptyWrapped = FilePayloadCodec.Wrap(Array.Empty<byte>(), "a.txt");
            var emptyBack = FilePayloadCodec.Unwrap(emptyWrapped);
            Check("empty payload round-trip",
                emptyBack.FileName == "a.txt" && emptyBack.Data.Length == 0);

            // 9) 切换编码要真正转码（写入模式），而不是用新编码重新解释旧字节
            static EncodingOption Enc(string name)
                => EncodingOption.Catalog.First(o => o.DisplayName.StartsWith(name, StringComparison.Ordinal));

            var gbk = Enc("GBK");
            var utf8 = Enc("UTF-8");
            var utf16 = Enc("UTF-16 LE");

            const string sample = "中文 abc 123";
            var utf8Bytes = Encoding.UTF8.GetBytes(sample);

            var session = new ViewModels.Editors.EditorSessionViewModel();
            session.LoadBytes(utf8Bytes, EditorChangeSource.Import, "note.txt");
            session.SelectedEncoding = gbk;

            Check("transcode utf8 -> gbk keeps text",
                session.Text == sample && session.Snapshot().SequenceEqual(gbk.Encoding.GetBytes(sample)),
                $"text=ok? {session.Text == sample} bytes={session.Snapshot().Length}");

            session.SelectedEncoding = utf8;
            Check("transcode gbk -> utf8 restores bytes",
                session.Text == sample && session.Snapshot().SequenceEqual(utf8Bytes),
                $"{session.Snapshot().Length} B");

            session.SelectedEncoding = utf16;
            Check("transcode utf8 -> utf16",
                session.Text == sample && session.Snapshot().SequenceEqual(Encoding.Unicode.GetBytes(sample)),
                $"{session.Snapshot().Length} B");

            session.SelectedEncoding = gbk;
            Check("transcode utf16 -> gbk (text stays readable)",
                session.Text == sample && session.Snapshot().SequenceEqual(gbk.Encoding.GetBytes(sample)),
                $"{session.Snapshot().Length} B");

            // 10) 二进制内容不能被转码改坏
            var binary = new byte[] { 0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0xFF, 0x42 };
            var binSession = new ViewModels.Editors.EditorSessionViewModel();
            binSession.LoadBytes(binary, EditorChangeSource.Import, "photo.png");
            binSession.SelectedEncoding = gbk;
            Check("binary payload untouched by encoding switch",
                binSession.Snapshot().SequenceEqual(binary), $"{binSession.Snapshot().Length} B");

            // 11) 只读会话（读取模式）只换显示编码，不改字节
            var readSession = new ViewModels.Editors.EditorSessionViewModel(isReadOnly: true);
            readSession.LoadBytes(utf8Bytes, EditorChangeSource.Device, "dump.bin");
            readSession.SelectedEncoding = gbk;
            Check("read-only session keeps bytes",
                readSession.Snapshot().SequenceEqual(utf8Bytes), $"{readSession.Snapshot().Length} B");

            // 12) 新编码表示不了的字符：不转码（保持原字节）
            var jp = Enc("Shift_JIS");
            var emojiSession = new ViewModels.Editors.EditorSessionViewModel();
            emojiSession.LoadBytes(Encoding.UTF8.GetBytes("emoji 😀"), EditorChangeSource.Import, "e.txt");
            emojiSession.SelectedEncoding = jp;
            Check("unrepresentable chars are not transcoded",
                emojiSession.Snapshot().SequenceEqual(Encoding.UTF8.GetBytes("emoji 😀")));

            // 13) 传输速率计
            var speed = new Models.TransferSpeedometer();
            speed.Start(64_000);
            var idle = speed.Sample(0);
            System.Threading.Thread.Sleep(150);
            var moving = speed.Sample(50);
            speed.Finish();

            Check("speedometer idle shows dash", idle == "—", idle);
            Check("speedometer reports a rate", moving.EndsWith("/s", StringComparison.Ordinal), moving);
            Check("speedometer stopped shows dash", speed.Sample(100) == "—");
            Check("speedometer average after finish",
                speed.AverageText.StartsWith("avg ", StringComparison.Ordinal), speed.AverageText);

            // 14) 数据哈希：FNV-1a 64（与模拟器 / 文档使用同一算法）
            Check("fnv1a empty",
                DataIntegrity.ComputeHashHex(Array.Empty<byte>()) == "CBF29CE484222325",
                DataIntegrity.ComputeHashHex(Array.Empty<byte>()));
            Check("fnv1a hello",
                DataIntegrity.ComputeHashHex(Encoding.ASCII.GetBytes("hello")) == "A430D84680AABD0B",
                DataIntegrity.ComputeHashHex(Encoding.ASCII.GetBytes("hello")));
            Check("hash compare ignores case",
                DataIntegrity.Matches("a430d84680aabd0b", "A430D84680AABD0B"));

            // 15) 读取校验的四种结论
            var payload = Encoding.ASCII.GetBytes("capsa-payload");
            var goodHash = DataIntegrity.ComputeHashHex(payload);

            Check("verify ok",
                DataIntegrity.Verify(payload, goodHash, payload.Length, out _) == DataIntegrity.Verdict.Ok);

            var corrupted = (byte[])payload.Clone();
            corrupted[3] ^= 0xFF;
            Check("verify detects corrupted byte",
                DataIntegrity.Verify(corrupted, goodHash, corrupted.Length, out var badHash)
                    == DataIntegrity.Verdict.Mismatch && badHash != goodHash);

            Check("verify without stored hash",
                DataIntegrity.Verify(payload, null, payload.Length, out _) == DataIntegrity.Verdict.NoRecord);

            Check("verify skipped when length differs",
                DataIntegrity.Verify(payload, goodHash, payload.Length + 100, out _) == DataIntegrity.Verdict.Skipped);

            // 16) 串口组探测：权限文案里的组名必须是**本机真实存在**的那个
            //     （Debian 系 dialout / Arch 系 uucp）。写死组名会让用户敲出
            //     "usermod: group 'dialout' does not exist"。
            if (!OperatingSystem.IsWindows())
            {
                var group = UnixSerialGroup.Current;
                var hint = UnixSerialGroup.BuildHint("/dev/ttyUSB0");

                Check("serial group hint names the local group",
                    hint.Contains(group.Group, StringComparison.Ordinal), hint);

                Check("serial group hint is actionable",
                    !group.GroupExists
                    || hint.Contains("usermod", StringComparison.Ordinal)
                    || hint.Contains("udev", StringComparison.Ordinal),
                    hint);

                if (group.GroupExists)
                {
                    var wrong = string.Equals(group.Group, "uucp", StringComparison.Ordinal) ? "dialout" : "uucp";
                    Check("serial group hint avoids the absent group name",
                        !hint.Contains(wrong, StringComparison.Ordinal),
                        $"{group.Group} exists, hint must not mention {wrong}");
                }
            }

            lines.Add(failed == 0 ? "SELF-TEST OK" : $"SELF-TEST FAILED ({failed})");

            var text = string.Join(Environment.NewLine, lines);
            if (!string.IsNullOrWhiteSpace(logPath))
            {
                try
                {
                    File.WriteAllText(logPath!, text, Encoding.UTF8);
                }
                catch (Exception)
                {
                    // 忽略日志写入失败
                }
            }

            Console.WriteLine(text);
            return failed == 0 ? 0 : 1;
        }
    }
}
