using System;
using System.Collections.Generic;
using System.IO;

namespace Capsa.Models
{
    /// <summary>
    /// Linux 上"哪个组能开串口"**不是固定的**：
    /// Debian / Ubuntu / Fedora 用 <c>dialout</c>，Arch / openSUSE / Manjaro 用 <c>uucp</c>。
    ///
    /// 之前把 <c>dialout</c> 写死在提示文案里，Arch 用户照着敲会得到
    /// <c>usermod: group 'dialout' does not exist</c> —— 一个纯粹由发行版差异造成的假故障。
    /// 这里改成运行时读 <c>/etc/group</c> 判断，同时顺手判断用户是不是**已经在**组里
    /// （是的话问题就不在权限，得往 udev / 重登录方向查）。
    ///
    /// 只读两个纯文本文件，不需要 P/Invoke，也不需要 <c>id</c> 子进程。
    /// </summary>
    public static class UnixSerialGroup
    {
        /// <summary>候选组名，按优先级排列。存在哪个用哪个。</summary>
        private static readonly string[] Candidates = { "dialout", "uucp" };

        private static readonly Lazy<Resolution> Detected = new(Detect, isThreadSafe: true);

        /// <summary>探测结果：组名、组是否存在、当前用户是否已在该组。</summary>
        public readonly record struct Resolution(string Group, bool GroupExists, bool UserInGroup);

        /// <summary>当前系统的探测结果（进程内缓存一次）。</summary>
        public static Resolution Current => Detected.Value;

        /// <summary>
        /// 生成"权限不足"的排查提示。调用方只在 Unix 分支用它。
        /// </summary>
        public static string BuildHint(string port)
        {
            var r = Current;

            // 组都不存在：极简系统（容器 / 定制发行版）里 tty 常常就是 root:root 0660
            if (!r.GroupExists)
            {
                return $"{port} 权限不足 —— 本机没有 dialout/uucp 组，请确认设备属主：" +
                       $"ls -l {port}（必要时用 udev 规则把组设为当前用户所在组）";
            }

            if (r.UserInGroup)
            {
                return $"{port} 权限不足 —— 当前用户已在 {r.Group} 组，但仍打不开：" +
                       "可能是本次登录尚未生效（注销后重新登录），或 udev 规则把设备改成了别的属组";
            }

            return $"{port} 权限不足 —— 当前用户不在 {r.Group} 组：" +
                   $"sudo usermod -aG {r.Group} $USER（注销后重新登录生效）";
        }

        /// <summary>给 UI 排查清单用的一行命令，已按发行版填好组名。</summary>
        public static string BuildCheckLine()
        {
            var r = Current;
            return r.GroupExists
                ? $"  • 当前用户是否在 {r.Group} 组：groups | grep {r.Group}" +
                  $"（不在则 sudo usermod -aG {r.Group} $USER，重新登录生效）；"
                : "  • 设备权限：ls -l /dev/ttyUSB0（属主应为 root，属组应为 dialout 或 uucp）；";
        }

        private static Resolution Detect()
        {
            string? group = null;

            try
            {
                if (!File.Exists("/etc/group"))
                {
                    return new Resolution("dialout", false, false);
                }

                var lines = File.ReadAllLines("/etc/group");
                var byName = new Dictionary<string, (int Gid, HashSet<string> Members)>(StringComparer.Ordinal);

                foreach (var line in lines)
                {
                    // name:passwd:gid:member1,member2
                    var parts = line.Split(':');
                    if (parts.Length < 4)
                    {
                        continue;
                    }

                    var members = new HashSet<string>(
                        parts[3].Split(',', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries),
                        StringComparer.Ordinal);

                    _ = int.TryParse(parts[2], out var gid);
                    byName[parts[0]] = (gid, members);
                }

                foreach (var candidate in Candidates)
                {
                    if (byName.ContainsKey(candidate))
                    {
                        group = candidate;
                        break;
                    }
                }

                if (group is null)
                {
                    return new Resolution("dialout", false, false);
                }

                var (groupId, groupMembers) = byName[group];
                var user = Environment.UserName;

                // 成员判定有两路：附加组（成员列表里有没有我）+ 主组（GID 相等，Arch 上常见）
                var inGroup = groupMembers.Contains(user);

                if (!inGroup && TryGetPrimaryGid(user, out var primaryGid))
                {
                    inGroup = primaryGid == groupId;
                }

                return new Resolution(group, true, inGroup);
            }
            catch
            {
                // 读不到就退回最常见的那套文案，不影响主流程
                return new Resolution(group ?? "dialout", group is not null, false);
            }
        }

        private static bool TryGetPrimaryGid(string user, out int gid)
        {
            gid = -1;

            try
            {
                if (!File.Exists("/etc/passwd"))
                {
                    return false;
                }

                foreach (var line in File.ReadAllLines("/etc/passwd"))
                {
                    // name:x:uid:gid:gecos:home:shell
                    var parts = line.Split(':');
                    if (parts.Length < 4 || !string.Equals(parts[0], user, StringComparison.Ordinal))
                    {
                        continue;
                    }

                    return int.TryParse(parts[3], out gid);
                }
            }
            catch
            {
                // 忽略：只是拿不到主组，附加组判定仍然有效
            }

            return false;
        }
    }
}
