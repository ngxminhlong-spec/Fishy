#include "I18n.hpp"
#include <algorithm>
#include <cctype>
#include <unordered_map>

namespace {

struct Row { const char* key; const char* en; const char* vi; const char* zh; };

// ---------------------------------------------------------------------------------
//  TRANSLATION TABLE  -  {key, English, Tiếng Việt, 简体中文}
//  Placeholders look like {name} and are filled in by trf().
// ---------------------------------------------------------------------------------
const Row ROWS[] = {

// ---------- language command ----------
{"lang.title", "🌐 Language", "🌐 Ngôn ngữ", "🌐 语言"},
{"lang.show",
 "Your language is **{lang}**{mode}.\nUse `/language` and pick an option to change it.",
 "Ngôn ngữ của bạn là **{lang}**{mode}.\nDùng `/language` và chọn một tùy chọn để thay đổi.",
 "你当前的语言是 **{lang}**{mode}。\n使用 `/language` 并选择一个选项即可更改。"},
{"lang.mode.auto",
 " (automatic, from your Discord settings)",
 " (tự động, theo cài đặt Discord của bạn)",
 "(自动，跟随你的 Discord 设置)"},
{"lang.mode.manual", "", "", ""},
{"lang.set", "✅ Language set to **{lang}**.", "✅ Đã đổi ngôn ngữ sang **{lang}**.", "✅ 语言已设置为 **{lang}**。"},
{"lang.auto_set",
 "✅ Language set to **automatic**. The bot will follow your Discord language (currently **{lang}**).",
 "✅ Đã chuyển sang **tự động**. Bot sẽ theo ngôn ngữ Discord của bạn (hiện tại: **{lang}**).",
 "✅ 已设置为**自动**。机器人将跟随你的 Discord 语言（当前：**{lang}**）。"},
{"lang.choice.auto", "Auto (follow Discord)", "Tự động (theo Discord)", "自动（跟随 Discord）"},

// ---------- slash-command descriptions (shown by Discord in the user's own language) ----------
{"cmd.language.desc",
 "Choose the language the bot uses with you.",
 "Chọn ngôn ngữ bot dùng với bạn.",
 "选择机器人对你使用的语言。"},
{"cmd.stats.desc",
 "Show bot, server and database statistics.",
 "Xem thống kê bot, máy chủ và cơ sở dữ liệu.",
 "查看机器人、主机和数据库的统计信息。"},
{"cmd.terms.desc", "Read the Terms of Service.", "Đọc Điều khoản dịch vụ.", "阅读服务条款。"},
{"cmd.privacy.desc", "Read the Privacy Policy.", "Đọc Chính sách quyền riêng tư.", "阅读隐私政策。"},
{"cmd.help.desc",
 "List everything the bot can do.",
 "Liệt kê mọi thứ bot có thể làm.",
 "列出机器人的全部功能。"},
{"cmd.fish.desc",
 "Cast your line at your current location!",
 "Quăng cần câu tại địa điểm hiện tại của bạn!",
 "在当前地点抛竿钓鱼！"},
{"cmd.balance.desc", "Check your coin balance.", "Xem số dư xu của bạn.", "查看你的金币余额。"},
{"cmd.daily.desc",
 "Claim your daily coin reward.",
 "Nhận thưởng xu hằng ngày.",
 "领取每日金币奖励。"},

// ---------- shared words ----------
{"unit.coins", "coins", "xu", "金币"},

// ---------- /balance, /daily ----------
{"balance.show", "💰 Your balance: **{coins}**", "💰 Số dư của bạn: **{coins}**", "💰 你的余额：**{coins}**"},
{"daily.cooldown",
 "⏳ You already claimed today. Come back in {hours}h.",
 "⏳ Hôm nay bạn đã nhận rồi. Hãy quay lại sau {hours} giờ.",
 "⏳ 你今天已经领取过了，请在 {hours} 小时后再来。"},
{"daily.claimed",
 "📅 Daily reward claimed: **{coins}** (streak: {streak} days)",
 "📅 Đã nhận thưởng hằng ngày: **{coins}** (chuỗi: {streak} ngày)",
 "📅 已领取每日奖励：**{coins}**（连续 {streak} 天）"},

// ---------- /help ----------
{"help.title", "🎣 Fishing Bot — Commands", "🎣 Bot câu cá — Danh sách lệnh", "🎣 钓鱼机器人 — 命令列表"},
{"help.body",
 "**/fish** — cast your line and reel it in\n"
 "**/shop** — buy rods, bait, boats, pets\n"
 "**/sell** — sell fish for coins\n"
 "**/trade** — trade fish & coins with another player\n"
 "**/inventory**, **/aquarium** — manage your fish\n"
 "**/balance**, **/daily**, **/gift** — economy\n"
 "**/location** — travel between fishing spots\n"
 "**/market**, **/weather** — check conditions\n"
 "**/profile**, **/fishdex**, **/achievements** — progress\n"
 "**/quest** — daily quest for bonus coins\n"
 "**/pet** — your fishing companion\n"
 "**/crew** — form a fishing crew with a shared bank\n"
 "**/boss** — World Boss fights the whole server can join\n"
 "**/leaderboard**, **/tournament** — competition\n"
 "**/event** — limited-time seasonal fish\n"
 "**/auction** — buy and sell fish with other players\n"
 "**/lottery** — daily jackpot draw\n"
 "**/enchant** — permanent upgrades (luck, reel power, sell price, bite chance)\n"
 "**/stats** — bot, host and database statistics\n"
 "**/terms**, **/privacy** — Terms of Service & Privacy Policy\n"
 "**/language** — change the bot's language\n",
 "**/fish** — quăng cần và kéo cá lên\n"
 "**/shop** — mua cần câu, mồi, thuyền, thú cưng\n"
 "**/sell** — bán cá lấy xu\n"
 "**/trade** — trao đổi cá và xu với người chơi khác\n"
 "**/inventory**, **/aquarium** — quản lý cá của bạn\n"
 "**/balance**, **/daily**, **/gift** — kinh tế\n"
 "**/location** — di chuyển giữa các điểm câu\n"
 "**/market**, **/weather** — xem thị trường và thời tiết\n"
 "**/profile**, **/fishdex**, **/achievements** — tiến trình\n"
 "**/quest** — nhiệm vụ hằng ngày nhận thêm xu\n"
 "**/pet** — bạn đồng hành câu cá\n"
 "**/crew** — lập đội câu cá với ngân hàng chung\n"
 "**/boss** — đánh World Boss cùng cả máy chủ\n"
 "**/leaderboard**, **/tournament** — thi đấu\n"
 "**/event** — cá sự kiện theo mùa, có thời hạn\n"
 "**/auction** — mua bán cá với người chơi khác\n"
 "**/lottery** — xổ số jackpot hằng ngày\n"
 "**/enchant** — nâng cấp vĩnh viễn (may mắn, sức kéo, giá bán, tỉ lệ cắn câu)\n"
 "**/stats** — thống kê bot, máy chủ và cơ sở dữ liệu\n"
 "**/terms**, **/privacy** — Điều khoản dịch vụ & Chính sách quyền riêng tư\n"
 "**/language** — đổi ngôn ngữ của bot\n",
 "**/fish** — 抛竿钓鱼并收线\n"
 "**/shop** — 购买鱼竿、鱼饵、船只和宠物\n"
 "**/sell** — 出售鱼类换取金币\n"
 "**/trade** — 与其他玩家交易鱼和金币\n"
 "**/inventory**、**/aquarium** — 管理你的鱼\n"
 "**/balance**、**/daily**、**/gift** — 经济系统\n"
 "**/location** — 在不同钓点之间移动\n"
 "**/market**、**/weather** — 查看行情与天气\n"
 "**/profile**、**/fishdex**、**/achievements** — 进度\n"
 "**/quest** — 每日任务，领取额外金币\n"
 "**/pet** — 你的钓鱼伙伴\n"
 "**/crew** — 组建拥有共享金库的钓鱼小队\n"
 "**/boss** — 全服玩家可共同参与的世界首领战\n"
 "**/leaderboard**、**/tournament** — 竞技\n"
 "**/event** — 限时赛季活动鱼\n"
 "**/auction** — 与其他玩家买卖鱼\n"
 "**/lottery** — 每日大奖抽奖\n"
 "**/enchant** — 永久强化（幸运、收线力、售价、咬钩率）\n"
 "**/stats** — 机器人、主机和数据库统计\n"
 "**/terms**、**/privacy** — 服务条款与隐私政策\n"
 "**/language** — 切换机器人语言\n"},

// ---------- /stats ----------
{"stats.title", "📊 Bot Statistics", "📊 Thống kê bot", "📊 机器人统计"},
{"stats.sec.bot", "🤖 Bot", "🤖 Bot", "🤖 机器人"},
{"stats.sec.proc", "⚙️ Process & CPU", "⚙️ Tiến trình & CPU", "⚙️ 进程与 CPU"},
{"stats.sec.mem", "🧠 Memory", "🧠 Bộ nhớ", "🧠 内存"},
{"stats.sec.host", "🖥️ Host system", "🖥️ Hệ thống chạy bot", "🖥️ 主机系统"},
{"stats.sec.db", "🗄️ Database size", "🗄️ Dung lượng cơ sở dữ liệu", "🗄️ 数据库大小"},
{"stats.sec.dbinfo", "🧱 Database details", "🧱 Chi tiết cơ sở dữ liệu", "🧱 数据库详情"},
{"stats.sec.rows", "📚 Rows per table", "📚 Số dòng theo bảng", "📚 各表行数"},

{"stats.uptime", "Uptime", "Thời gian hoạt động", "运行时间"},
{"stats.servers", "Discord servers", "Máy chủ Discord", "Discord 服务器"},
{"stats.players", "Players", "Người chơi", "玩家数"},
{"stats.fish_total", "Fish stored", "Cá đang lưu", "库存鱼数量"},
{"stats.cmds", "Commands run", "Lệnh đã chạy", "已执行命令"},
{"stats.ping", "API latency", "Độ trễ API", "API 延迟"},
{"stats.shards", "Shards", "Shard", "分片"},
{"stats.lib", "Library", "Thư viện", "依赖库"},

{"stats.pid", "PID", "PID", "PID"},
{"stats.threads", "Threads", "Số luồng", "线程数"},
{"stats.cpu", "CPU usage", "CPU sử dụng", "CPU 占用"},
{"stats.cpu.fmt", "{core}% of 1 core ({total}% of all {cores} cores)",
                  "{core}% của 1 nhân ({total}% trên tổng {cores} nhân)",
                  "单核 {core}%（全部 {cores} 核共 {total}%）"},
{"stats.cputime", "CPU time used", "Thời gian CPU đã dùng", "累计 CPU 时间"},

{"stats.ram", "RAM (bot)", "RAM (bot)", "内存 RAM（机器人）"},
{"stats.vv", "Virtual memory (VV)", "Bộ nhớ ảo (VV)", "虚拟内存 (VV)"},
{"stats.vvpeak", "Virtual memory peak", "Đỉnh bộ nhớ ảo", "虚拟内存峰值"},
{"stats.swap", "Swap (bot)", "Swap (bot)", "交换区 Swap（机器人）"},

{"stats.os", "OS", "Hệ điều hành", "操作系统"},
{"stats.cpumodel", "CPU", "CPU", "CPU"},
{"stats.cores", "Cores", "Số nhân", "核心数"},
{"stats.load", "Load average (1/5/15 min)", "Tải trung bình (1/5/15 phút)", "平均负载（1/5/15 分钟）"},
{"stats.hostram", "System RAM (used / total)", "RAM hệ thống (đã dùng / tổng)", "系统内存（已用 / 总计）"},
{"stats.hostswap", "System swap (used / total)", "Swap hệ thống (đã dùng / tổng)", "系统 Swap（已用 / 总计）"},
{"stats.hostup", "Host uptime", "Máy đã chạy được", "主机运行时间"},

{"stats.dbmain", "Main file", "Tệp chính", "主文件"},
{"stats.dbwal", "WAL + SHM files", "Tệp WAL + SHM", "WAL + SHM 文件"},
{"stats.dbtotal", "Total on disk", "Tổng trên ổ đĩa", "磁盘总占用"},
{"stats.bits", "bits", "bit", "比特"},
{"stats.bytes", "bytes", "byte", "字节"},
{"stats.sqlite", "SQLite version", "Phiên bản SQLite", "SQLite 版本"},
{"stats.pages", "Pages (size × count)", "Trang (kích thước × số lượng)", "页（大小 × 数量）"},
{"stats.freepages", "Free pages", "Trang trống", "空闲页"},
{"stats.sqlmem", "SQLite memory", "Bộ nhớ SQLite", "SQLite 内存占用"},

{"stats.na", "N/A (not available on this system)", "Không khả dụng trên hệ thống này", "此系统不可用"},
{"stats.footer",
 "1 KB = 1024 bytes • CPU sampled over 250 ms • updated",
 "1 KB = 1024 byte • CPU đo trong 250 ms • cập nhật lúc",
 "1 KB = 1024 字节 • CPU 采样 250 毫秒 • 更新于"},

// ---------- Terms of Service ----------
{"terms.title", "📜 Terms of Service", "📜 Điều khoản dịch vụ", "📜 服务条款"},
{"terms.body",
 "**Last updated:** {date}\n\n"
 "**1. Acceptance** — By using this bot you agree to these Terms. If you don't agree, please stop using it. "
 "You must also follow Discord's Terms of Service and Community Guidelines.\n\n"
 "**2. Virtual items only** — Coins, fish, rods, pets and everything else are virtual game items. They have no "
 "real-world value, can't be bought, sold or cashed out for money, and you don't own them.\n\n"
 "**3. Fair play** — No cheating, exploiting bugs, automation or self-bots, farming rewards with multiple "
 "accounts, or trading items/accounts for real money. Please report bugs instead of abusing them.\n\n"
 "**4. Enforcement** — The operator may reset or remove coins, items or progress, block users, or remove the bot "
 "from a server at any time, with or without notice, for rule violations or to fix bugs and exploits.\n\n"
 "**5. Changes to the game** — Prices, odds, items and features can change or be removed. Progress may be "
 "adjusted or restored from a backup.\n\n"
 "**6. No warranty** — The bot is provided \"as is\" and \"as available\". Downtime, bugs and data loss can happen, "
 "and to the extent the law allows, the operator isn't liable for lost progress or items.\n\n"
 "**7. Lottery & auction** — These use virtual coins only. No real money is involved and there are no "
 "real-world prizes.\n\n"
 "**8. Updates** — These Terms may change. Continuing to use the bot after a change means you accept it.\n\n"
 "**9. Contact** — {contact}\n\n"
 "See also **/privacy**.{full}",
 "**Cập nhật lần cuối:** {date}\n\n"
 "**1. Chấp nhận** — Khi sử dụng bot này, bạn đồng ý với các Điều khoản này. Nếu không đồng ý, vui lòng ngừng sử "
 "dụng. Bạn cũng phải tuân thủ Điều khoản dịch vụ và Nguyên tắc cộng đồng của Discord.\n\n"
 "**2. Chỉ là vật phẩm ảo** — Xu, cá, cần câu, thú cưng và mọi thứ khác đều là vật phẩm ảo trong game. Chúng "
 "không có giá trị thực tế, không thể mua, bán hay quy đổi ra tiền, và bạn không sở hữu chúng.\n\n"
 "**3. Chơi công bằng** — Không gian lận, lợi dụng lỗi, dùng tự động hóa hoặc self-bot, dùng nhiều tài khoản để "
 "cày phần thưởng, hay trao đổi vật phẩm/tài khoản lấy tiền thật. Hãy báo lỗi thay vì lợi dụng chúng.\n\n"
 "**4. Xử lý vi phạm** — Người vận hành có thể đặt lại hoặc xóa xu, vật phẩm, tiến trình, chặn người dùng hoặc "
 "đưa bot ra khỏi máy chủ bất cứ lúc nào, có hoặc không báo trước, khi có vi phạm hoặc để sửa lỗi.\n\n"
 "**5. Thay đổi trong game** — Giá cả, tỉ lệ, vật phẩm và tính năng có thể thay đổi hoặc bị gỡ bỏ. Tiến trình có "
 "thể được điều chỉnh hoặc khôi phục từ bản sao lưu.\n\n"
 "**6. Không bảo đảm** — Bot được cung cấp \"nguyên trạng\" và \"theo khả năng sẵn có\". Có thể xảy ra gián đoạn, "
 "lỗi và mất dữ liệu; trong phạm vi pháp luật cho phép, người vận hành không chịu trách nhiệm về tiến trình hay "
 "vật phẩm bị mất.\n\n"
 "**7. Xổ số & đấu giá** — Chỉ dùng xu ảo. Không liên quan đến tiền thật và không có giải thưởng thực tế.\n\n"
 "**8. Cập nhật** — Các Điều khoản này có thể thay đổi. Tiếp tục sử dụng bot sau khi thay đổi nghĩa là bạn chấp "
 "nhận chúng.\n\n"
 "**9. Liên hệ** — {contact}\n\n"
 "Xem thêm **/privacy**.{full}",
 "**最后更新：**{date}\n\n"
 "**1. 接受条款** — 使用本机器人即表示你同意这些条款。如果不同意，请停止使用。你同时必须遵守 Discord 的服务条款和社区准则。\n\n"
 "**2. 仅限虚拟物品** — 金币、鱼、鱼竿、宠物等一切内容都是游戏内的虚拟物品，没有任何现实价值，不能购买、出售或兑换成现金，"
 "你也不拥有它们的所有权。\n\n"
 "**3. 公平游戏** — 禁止作弊、利用漏洞、使用自动化脚本或自走号、用多个账号刷取奖励，以及用现实货币交易物品或账号。"
 "发现漏洞请及时报告，不要滥用。\n\n"
 "**4. 处理措施** — 因违规或为修复漏洞，运营者可随时（无论是否提前通知）重置或移除金币、物品或进度，封禁用户，"
 "或将机器人移出服务器。\n\n"
 "**5. 游戏变更** — 价格、概率、物品和功能可能调整或下线。进度可能被修正或从备份中恢复。\n\n"
 "**6. 免责声明** — 机器人按“现状”和“可用状态”提供。可能出现停机、错误和数据丢失；在法律允许的范围内，"
 "运营者不对丢失的进度或物品负责。\n\n"
 "**7. 抽奖与拍卖** — 仅使用虚拟金币，不涉及真实货币，也没有现实奖品。\n\n"
 "**8. 条款更新** — 本条款可能更新。更新后继续使用机器人即视为你接受更新。\n\n"
 "**9. 联系方式** — {contact}\n\n"
 "另请参阅 **/privacy**。{full}"},

// ---------- Privacy Policy ----------
{"privacy.title", "🔒 Privacy Policy", "🔒 Chính sách quyền riêng tư", "🔒 隐私政策"},
{"privacy.body",
 "**Last updated:** {date}\n\n"
 "**What is stored** — Your Discord user ID (a number, not your name, avatar or email) together with your game "
 "data: coins, gear, fish, fishdex, achievements, quests, enchants, crew membership, auction and lottery entries, "
 "trades in progress, tournament score and your language choice. For servers: the server ID and the boss "
 "channel, if an admin set one. Crew names are text you type yourself.\n\n"
 "**What is NOT stored** — Message content, DMs, usernames, avatars, email addresses, IP addresses or payment "
 "details. The bot only reacts to slash commands and buttons.\n\n"
 "**How it's used** — Only to run the game: saving progress, leaderboards, trading, and showing messages in your "
 "language.\n\n"
 "**Who can see it** — Other players in a server can see your name or mention in leaderboards, trades, auctions, "
 "crews and boss results. Crew names are public. Your data is not sold or shared with third parties; it lives on "
 "the operator's server and passes through Discord, which every bot needs in order to work.\n\n"
 "**Logs** — Technical logs (errors, server joins/leaves) may be kept briefly by the host for troubleshooting.\n\n"
 "**Retention & deletion** — Data is kept while the bot is in use. You can ask the operator to erase everything "
 "stored about you (see contact below); backups are overwritten as they rotate out.\n\n"
 "**Age** — You must meet Discord's minimum age to use this bot.\n\n"
 "**Your language** — Change it any time with **/language**; choosing *Auto* removes the saved preference.\n\n"
 "**Security** — Reasonable steps are taken, but no system is 100% secure.\n\n"
 "**Contact** — {contact}\n\n"
 "See also **/terms**.{full}",
 "**Cập nhật lần cuối:** {date}\n\n"
 "**Dữ liệu được lưu** — ID người dùng Discord của bạn (một dãy số, không phải tên, ảnh đại diện hay email) cùng "
 "dữ liệu trò chơi: xu, trang bị, cá, fishdex, thành tựu, nhiệm vụ, phù phép, đội câu cá, lượt đấu giá và xổ số, "
 "giao dịch đang diễn ra, điểm giải đấu và lựa chọn ngôn ngữ. Với máy chủ: ID máy chủ và kênh boss nếu quản trị "
 "viên đã đặt. Tên đội là nội dung do chính bạn nhập.\n\n"
 "**Dữ liệu KHÔNG được lưu** — Nội dung tin nhắn, tin nhắn riêng, tên người dùng, ảnh đại diện, email, địa chỉ IP "
 "hay thông tin thanh toán. Bot chỉ phản hồi lệnh slash và nút bấm.\n\n"
 "**Mục đích sử dụng** — Chỉ để vận hành trò chơi: lưu tiến trình, bảng xếp hạng, giao dịch và hiển thị tin nhắn "
 "bằng ngôn ngữ của bạn.\n\n"
 "**Ai có thể xem** — Người chơi khác trong máy chủ có thể thấy tên hoặc lượt nhắc của bạn trong bảng xếp hạng, "
 "giao dịch, đấu giá, đội và kết quả boss. Tên đội là công khai. Dữ liệu không bị bán hay chia sẻ cho bên thứ ba; "
 "dữ liệu nằm trên máy chủ của người vận hành và đi qua Discord, điều bắt buộc để mọi bot hoạt động.\n\n"
 "**Nhật ký** — Máy chủ có thể giữ nhật ký kỹ thuật (lỗi, việc bot vào/rời máy chủ) trong thời gian ngắn để khắc "
 "phục sự cố.\n\n"
 "**Lưu trữ & xóa** — Dữ liệu được giữ trong suốt thời gian bot hoạt động. Bạn có thể yêu cầu người vận hành xóa "
 "toàn bộ dữ liệu về bạn (xem liên hệ bên dưới); các bản sao lưu sẽ bị ghi đè khi luân chuyển.\n\n"
 "**Độ tuổi** — Bạn phải đủ độ tuổi tối thiểu của Discord để dùng bot này.\n\n"
 "**Ngôn ngữ** — Đổi bất cứ lúc nào bằng **/language**; chọn *Tự động* sẽ xóa tùy chọn đã lưu.\n\n"
 "**Bảo mật** — Chúng tôi áp dụng các biện pháp hợp lý nhưng không hệ thống nào an toàn tuyệt đối.\n\n"
 "**Liên hệ** — {contact}\n\n"
 "Xem thêm **/terms**.{full}",
 "**最后更新：**{date}\n\n"
 "**会存储的内容** — 你的 Discord 用户 ID（一串数字，不是你的名字、头像或邮箱）以及游戏数据：金币、装备、鱼、图鉴、成就、"
 "任务、附魔、小队成员身份、拍卖与抽奖记录、进行中的交易、锦标赛分数和你选择的语言。关于服务器：服务器 ID，"
 "以及管理员设置的首领频道（如有）。小队名称是你自己输入的文字。\n\n"
 "**不会存储的内容** — 消息内容、私信、用户名、头像、邮箱地址、IP 地址或支付信息。机器人只响应斜杠命令和按钮。\n\n"
 "**用途** — 仅用于运行游戏：保存进度、排行榜、交易，以及用你的语言显示消息。\n\n"
 "**谁能看到** — 同一服务器的其他玩家可以在排行榜、交易、拍卖、小队和首领战结果中看到你的名字或提及。小队名称是公开的。"
 "你的数据不会被出售或与第三方共享；数据保存在运营者的服务器上，并会经过 Discord（任何机器人运行都需要如此）。\n\n"
 "**日志** — 主机可能会短期保留技术日志（错误、机器人加入/退出服务器）用于排查问题。\n\n"
 "**保存与删除** — 机器人运行期间会持续保存数据。你可以请求运营者删除所有关于你的数据（见下方联系方式）；"
 "备份会在轮换时被覆盖。\n\n"
 "**年龄** — 你必须达到 Discord 规定的最低年龄才能使用本机器人。\n\n"
 "**语言** — 随时可用 **/language** 更改；选择“自动”会删除已保存的偏好。\n\n"
 "**安全** — 我们会采取合理措施，但没有任何系统是绝对安全的。\n\n"
 "**联系方式** — {contact}\n\n"
 "另请参阅 **/terms**。{full}"},

{"legal.contact.default",
 "Contact the bot's operator through the server where you found this bot (ask a server admin).",
 "Hãy liên hệ người vận hành bot qua máy chủ nơi bạn tìm thấy bot (hỏi quản trị viên máy chủ).",
 "请通过你发现本机器人的服务器联系运营者（可询问服务器管理员）。"},
{"legal.full", "\n\n📄 Full text: {url}", "\n\n📄 Toàn văn: {url}", "\n\n📄 完整文本：{url}"},
};

const std::unordered_map<std::string, const Row*>& table() {
    static const std::unordered_map<std::string, const Row*> t = [] {
        std::unordered_map<std::string, const Row*> m;
        for (const Row& r : ROWS) m[r.key] = &r;
        return m;
    }();
    return t;
}

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    return s;
}

} // namespace

const char* lang_code(Lang l) {
    switch (l) {
        case Lang::VI: return "vi";
        case Lang::ZH: return "zh";
        default:       return "en";
    }
}

const char* lang_native_name(Lang l) {
    switch (l) {
        case Lang::VI: return "Tiếng Việt";
        case Lang::ZH: return "中文（简体）";
        default:       return "English";
    }
}

std::optional<Lang> parse_lang(const std::string& code) {
    std::string c = lower(code);
    if (c == "en" || c.rfind("en-", 0) == 0 || c.rfind("en_", 0) == 0) return Lang::EN;
    if (c == "vi" || c.rfind("vi-", 0) == 0 || c.rfind("vi_", 0) == 0) return Lang::VI;
    if (c == "zh" || c.rfind("zh-", 0) == 0 || c.rfind("zh_", 0) == 0) return Lang::ZH;
    return std::nullopt;
}

Lang lang_from_discord_locale(const std::string& locale) {
    return parse_lang(locale).value_or(Lang::EN);
}

std::string tr(Lang l, const std::string& key) {
    auto it = table().find(key);
    if (it == table().end()) return key;
    const Row& r = *it->second;
    const char* s = r.en;
    if (l == Lang::VI && r.vi && *r.vi) s = r.vi;
    else if (l == Lang::ZH && r.zh && *r.zh) s = r.zh;
    return std::string(s);
}

std::string trf(Lang l, const std::string& key,
                std::initializer_list<std::pair<const char*, std::string>> args) {
    std::string s = tr(l, key);
    for (const auto& a : args) {
        const std::string needle = std::string("{") + a.first + "}";
        size_t pos = 0;
        while ((pos = s.find(needle, pos)) != std::string::npos) {
            s.replace(pos, needle.size(), a.second);
            pos += a.second.size();
        }
    }
    return s;
}
