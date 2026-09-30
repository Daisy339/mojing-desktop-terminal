import 'dart:async';

import 'package:file_picker/file_picker.dart';
import 'package:flutter/foundation.dart';
import 'package:flutter/material.dart';
import 'package:shared_preferences/shared_preferences.dart';

import 'album.dart';
import 'device.dart';

void main() => runApp(const OClockApp());

class OClockApp extends StatelessWidget {
  const OClockApp({super.key});
  @override
  Widget build(BuildContext context) => MaterialApp(
        title: 'O’Clock',
        debugShowCheckedModeBanner: false,
        theme: ThemeData(
          useMaterial3: true,
          scaffoldBackgroundColor: const Color(0xfff3efe4),
          colorScheme: ColorScheme.fromSeed(seedColor: const Color(0xff1f5c53)),
        ),
        home: const HomePage(),
      );
}

class HomePage extends StatefulWidget {
  const HomePage({super.key});
  @override
  State<HomePage> createState() => _HomePageState();
}

class _HomePageState extends State<HomePage> {
  final address = TextEditingController(),
      city = TextEditingController(),
      ssid = TextEditingController(),
      password = TextEditingController(),
      interval = TextEditingController(text: '1200');
  DeviceApi? api;
  DeviceSnapshot? snapshot;
  String? error;
  bool busy = false;
  int tab = 0;
  Timer? timer;
  @override
  void initState() {
    super.initState();
    restore();
    timer = Timer.periodic(const Duration(seconds: 8), (_) {
      if (!busy && api != null) refresh();
    });
  }

  Future<void> restore() async {
    final p = await SharedPreferences.getInstance();
    if (mounted) address.text = p.getString('device') ?? '';
  }

  @override
  void dispose() {
    timer?.cancel();
    for (final c in [address, city, ssid, password, interval]) {
      c.dispose();
    }
    super.dispose();
  }

  Future<void> connect() async {
    if (busy) return;
    setState(() {
      busy = true;
      error = null;
    });
    try {
      final next = DeviceApi(address.text);
      final value = await next.load();
      await (await SharedPreferences.getInstance()).setString(
        'device',
        address.text.trim(),
      );
      if (!mounted) return;
      api = next;
      snapshot = value;
      city.text = value.city;
      ssid.text = '${value.status['sta_ssid'] ?? ''}';
    } catch (e) {
      if (mounted) setState(() => error = '$e');
    } finally {
      if (mounted) setState(() => busy = false);
    }
  }

  bool refreshing = false;
  Future<void> refresh() async {
    final target = api;
    if (busy || refreshing || target == null) return;
    refreshing = true;
    try {
      final value = await target.load();
      if (mounted && identical(target, api)) {
        setState(() {
          snapshot = value;
          error = null;
        });
      }
    } catch (e) {
      if (mounted && identical(target, api)) {
        setState(() => error = '刷新失败，以下为上次数据：$e');
      }
    } finally {
      refreshing = false;
    }
  }

  Future<void> submit(
    String path,
    Map<String, dynamic> body, {
    bool json = false,
    bool network = false,
  }) async {
    final target = api;
    if (target == null || busy) return;
    setState(() {
      busy = true;
      error = null;
    });
    try {
      await target.post(path, body, json: json);
      if (!mounted) return;
      if (network) {
        password.clear();
        setState(() {
          api = null;
          snapshot = null;
          error = '已提交网络切换，请使用新地址重新连接';
        });
      } else {
        ScaffoldMessenger.of(context)
            .showSnackBar(const SnackBar(content: Text('操作成功')));
      }
    } catch (e) {
      if (mounted) setState(() => error = '$e');
    } finally {
      if (mounted) setState(() => busy = false);
    }
    if (mounted && !network && error == null) await refresh();
  }

  Future<bool> confirm(String title, String message) async =>
      await showDialog<bool>(
        context: context,
        builder: (c) => AlertDialog(
          title: Text(title),
          content: Text(message),
          actions: [
            TextButton(
              onPressed: () => Navigator.pop(c, false),
              child: const Text('取消'),
            ),
            FilledButton(
              onPressed: () => Navigator.pop(c, true),
              child: const Text('确认'),
            ),
          ],
        ),
      ) ??
      false;
  @override
  Widget build(BuildContext context) {
    final data = snapshot;
    final wide = MediaQuery.sizeOf(context).width >= 800;
    return Scaffold(
      appBar: AppBar(
        title: const Text('O’Clock 控制台'),
        actions: [
          IconButton(
            onPressed: busy
                ? null
                : data == null
                    ? connect
                    : refresh,
            icon: const Icon(Icons.refresh),
          ),
        ],
        bottom: busy
            ? const PreferredSize(
                preferredSize: Size.fromHeight(3),
                child: LinearProgressIndicator(),
              )
            : null,
      ),
      body: Row(
        children: [
          if (wide && data != null)
            NavigationRail(
              selectedIndex: tab,
              labelType: NavigationRailLabelType.all,
              onDestinationSelected: (i) => setState(() => tab = i),
              destinations: const [
                NavigationRailDestination(
                  icon: Icon(Icons.settings),
                  label: Text('设备'),
                ),
                NavigationRailDestination(
                  icon: Icon(Icons.checklist),
                  label: Text('待办'),
                ),
                NavigationRailDestination(
                  icon: Icon(Icons.photo_library),
                  label: Text('相册'),
                ),
              ],
            ),
          Expanded(
            child: Center(
              child: ConstrainedBox(
                constraints: const BoxConstraints(maxWidth: 1000),
                child: AbsorbPointer(
                  absorbing: busy,
                  child: data == null
                      ? connectView()
                      : RefreshIndicator(
                          onRefresh: refresh,
                          child: ListView(
                            physics: const AlwaysScrollableScrollPhysics(),
                            padding: const EdgeInsets.all(20),
                            children: [
                              if (error != null)
                                Text(
                                  error!,
                                  style: const TextStyle(color: Colors.red),
                                ),
                              statusView(data),
                              if (tab == 0) settingsView(data),
                              if (tab == 1) todosView(data),
                              if (tab == 2) albumView(),
                            ],
                          ),
                        ),
                ),
              ),
            ),
          ),
        ],
      ),
      bottomNavigationBar: wide || data == null
          ? null
          : NavigationBar(
              selectedIndex: tab,
              onDestinationSelected: (i) => setState(() => tab = i),
              destinations: const [
                NavigationDestination(icon: Icon(Icons.settings), label: '设备'),
                NavigationDestination(icon: Icon(Icons.checklist), label: '待办'),
                NavigationDestination(
                  icon: Icon(Icons.photo_library),
                  label: '相册',
                ),
              ],
            ),
    );
  }

  Widget connectView() => Center(
        child: Padding(
          padding: const EdgeInsets.all(24),
          child: Column(
            mainAxisSize: MainAxisSize.min,
            children: [
              const Icon(Icons.watch_outlined, size: 72),
              const Text(
                '连接 O’Clock',
                style: TextStyle(fontSize: 28, fontWeight: FontWeight.bold),
              ),
              const SizedBox(height: 12),
              const Text('填写 ESP32 的局域网 IP，不是电脑语音网关地址。'),
              const SizedBox(height: 12),
              TextField(
                controller: address,
                decoration: const InputDecoration(
                  labelText: 'ESP32 地址',
                  hintText: '例如 192.168.1.20',
                  border: OutlineInputBorder(),
                ),
                onSubmitted: (_) => connect(),
              ),
              const SizedBox(height: 12),
              FilledButton.icon(
                onPressed: busy ? null : connect,
                icon: const Icon(Icons.link),
                label: Text(busy ? '连接中…' : '连接'),
              ),
              if (error != null)
                Text(error!, style: const TextStyle(color: Colors.red)),
            ],
          ),
        ),
      );
  Widget section(String title, List<Widget> children) => Card(
        child: Padding(
          padding: const EdgeInsets.all(18),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.stretch,
            children: [
              Text(title, style: Theme.of(context).textTheme.titleLarge),
              const SizedBox(height: 12),
              ...children,
            ],
          ),
        ),
      );
  Widget statusView(DeviceSnapshot d) => section('设备状态', [
        Wrap(
          spacing: 18,
          runSpacing: 8,
          children: [
            Text('模式 ${d.status['mode'] ?? '-'}'),
            Text('IP ${d.status['ip'] ?? '-'}'),
            Text(d.status['wifi_connected'] == true ? 'Wi-Fi 已连接' : 'Wi-Fi 离线'),
            Text(
              '待办 ${d.status['open_todos'] ?? 0} / 已完成 ${d.status['completed_todos'] ?? 0}',
            ),
          ],
        ),
      ]);
  Widget settingsView(DeviceSnapshot d) => Column(
        children: [
          section('网络与天气', [
            Text(
              '校时：${d.status['ntp_synced'] == true ? '已同步' : '等待同步'} · AP：${d.status['ap_ssid'] ?? '-'}',
            ),
            TextField(
              controller: city,
              decoration: const InputDecoration(labelText: '天气城市'),
            ),
            FilledButton(
              onPressed: () => submit('/weather', {'city': city.text}),
              child: const Text('保存天气城市'),
            ),
            OutlinedButton(
              onPressed: () async {
                if (await confirm('进入 AP 配网', '设备会断开当前网络，确定继续？')) {
                  submit('/ap', {}, network: true);
                }
              },
              child: const Text('进入 AP 配网'),
            ),
          ]),
          section('Wi-Fi 配置', [
            Text('当前网络：${d.status['sta_ssid'] ?? '-'}'),
            TextField(
              controller: ssid,
              decoration: const InputDecoration(labelText: 'SSID'),
            ),
            TextField(
              controller: password,
              obscureText: true,
              decoration: const InputDecoration(labelText: '密码'),
            ),
            FilledButton(
              onPressed: () async {
                if (await confirm('保存 Wi-Fi', '设备会切换到新 Wi-Fi，确定继续？')) {
                  submit(
                      '/wifi',
                      {
                        'ssid': ssid.text,
                        'password': password.text,
                      },
                      network: true);
                }
              },
              child: const Text('保存 Wi-Fi'),
            ),
          ]),
          OutlinedButton(
            onPressed: () => setState(() {
              snapshot = null;
              api = null;
            }),
            child: const Text('更换设备地址'),
          ),
        ],
      );
  Widget todosView(DeviceSnapshot d) => Column(
        children: [
          section(
              '新建待办', [TodoEditor(onSave: (v) => submit('/todos/create', v))]),
          if (d.todos.isEmpty) section('暂无待办', [const Text('添加一条待办后会同步到设备。')]),
          ...d.todos.map(
            (t) => section(t.title, [
              Text(
                '${t.date} · ${label(t.priority)}优先级 · ${t.completed ? '已完成' : '待办中'}',
              ),
              Wrap(
                children: [
                  TextButton.icon(
                    onPressed: () => editTodo(t),
                    icon: const Icon(Icons.edit_outlined),
                    label: const Text('编辑'),
                  ),
                  TextButton.icon(
                    onPressed: () => submit('/todos/complete', {
                      'id': t.id,
                      'completed': t.completed ? 0 : 1,
                    }),
                    icon: const Icon(Icons.done),
                    label: Text(t.completed ? '恢复' : '完成'),
                  ),
                  TextButton.icon(
                    onPressed: () async {
                      if (await confirm('删除待办', '确定删除“${t.title}”？')) {
                        await submit('/todos/delete', {'id': t.id});
                      }
                    },
                    icon: const Icon(Icons.delete_outline),
                    label: const Text('删除'),
                  ),
                ],
              ),
            ]),
          ),
        ],
      );
  Future<void> editTodo(Todo todo) async {
    final values = await showDialog<Map<String, dynamic>>(
      context: context,
      builder: (c) => AlertDialog(
        title: const Text('编辑待办'),
        content: SizedBox(
          width: 400,
          child: SingleChildScrollView(
            child: TodoEditor(
              initial: todo,
              onSave: (v) async => Navigator.pop(c, v),
            ),
          ),
        ),
        actions: [
          TextButton(
            onPressed: () => Navigator.pop(c),
            child: const Text('取消'),
          ),
        ],
      ),
    );
    if (values != null && mounted) {
      await submit('/todos/update', {'id': todo.id, ...values});
    }
  }

  Widget albumView() => section('相册', [
        const Text('选择图片或 GIF，最多 8 帧，自动转成设备使用的 400×300 黑白帧。'),
        TextField(
          controller: interval,
          keyboardType: TextInputType.number,
          decoration: const InputDecoration(labelText: '帧间隔（200–5000 毫秒）'),
        ),
        FilledButton.icon(
          onPressed: uploadAlbum,
          icon: const Icon(Icons.upload),
          label: const Text('选择并上传'),
        ),
        OutlinedButton(
          onPressed: () async {
            if (await confirm('清空相册', '确定删除设备相册？')) submit('/album/clear', {});
          },
          child: const Text('清空相册'),
        ),
      ]);
  Future<void> uploadAlbum() async {
    if (busy || api == null) return;
    final target = api!;
    final n = int.tryParse(interval.text);
    if (n == null || n < 200 || n > 5000) {
      setState(() => error = '帧间隔必须为 200–5000 毫秒');
      return;
    }
    setState(() {
      busy = true;
      error = null;
    });
    try {
      final pick = await FilePicker.platform.pickFiles(
        allowMultiple: true,
        type: FileType.custom,
        allowedExtensions: ['png', 'jpg', 'jpeg', 'gif', 'bmp', 'webp'],
        withData: true,
      );
      if (pick == null) return;
      if (pick.files.any((f) => f.bytes == null)) {
        throw const FormatException('无法读取所选图片');
      }
      final frames = await compute(
        encodeAlbum,
        pick.files.map((f) => f.bytes!).toList(),
      );
      if (frames.isEmpty) throw const FormatException('没有可上传的帧');
      await target.post(
          '/album/upload',
          {
            'width': 400,
            'height': 300,
            'interval_ms': n,
            'frames': frames,
          },
          json: true);
      if (mounted) {
        ScaffoldMessenger.of(context)
            .showSnackBar(SnackBar(content: Text('已上传 ${frames.length} 帧')));
      }
    } catch (e) {
      if (mounted) setState(() => error = '$e');
    } finally {
      if (mounted) setState(() => busy = false);
    }
  }
}

String label(String value) =>
    {'high': '高', 'medium': '中', 'low': '低'}[value] ?? '中';

class TodoEditor extends StatefulWidget {
  final Future<void> Function(Map<String, dynamic>) onSave;
  final Todo? initial;
  const TodoEditor({super.key, required this.onSave, this.initial});
  @override
  State<TodoEditor> createState() => _TodoEditorState();
}

class _TodoEditorState extends State<TodoEditor> {
  final form = GlobalKey<FormState>();
  late final TextEditingController title, date;
  late String priority;
  @override
  void initState() {
    super.initState();
    title = TextEditingController(text: widget.initial?.title ?? '');
    date = TextEditingController(text: widget.initial?.date ?? '');
    priority = widget.initial?.priority ?? 'medium';
  }

  @override
  void dispose() {
    title.dispose();
    date.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) => Form(
        key: form,
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            TextFormField(
              controller: title,
              decoration: const InputDecoration(labelText: '标题'),
              validator: (v) => v == null || v.trim().isEmpty ? '请填写标题' : null,
            ),
            TextFormField(
              controller: date,
              readOnly: true,
              decoration: const InputDecoration(
                labelText: '截止日期',
                suffixIcon: Icon(Icons.calendar_month),
              ),
              validator: (v) => v == null || v.isEmpty ? '请选择日期' : null,
              onTap: () async {
                final d = await showDatePicker(
                  context: context,
                  initialDate: DateTime.tryParse(date.text) ?? DateTime.now(),
                  firstDate: DateTime(2000),
                  lastDate: DateTime(2099, 12, 31),
                );
                if (d != null && mounted) {
                  date.text =
                      '${d.year}-${d.month.toString().padLeft(2, '0')}-${d.day.toString().padLeft(2, '0')}';
                }
              },
            ),
            DropdownButtonFormField<String>(
              initialValue: priority,
              items: ['high', 'medium', 'low']
                  .map((p) => DropdownMenuItem(value: p, child: Text(label(p))))
                  .toList(),
              onChanged: (v) => setState(() => priority = v ?? 'medium'),
              decoration: const InputDecoration(labelText: '优先级'),
            ),
            FilledButton(
              onPressed: () {
                if (form.currentState!.validate()) {
                  widget.onSave({
                    'title': title.text.trim(),
                    'due_date': date.text,
                    'priority': priority,
                  });
                }
              },
              child: Text(widget.initial == null ? '添加待办' : '保存修改'),
            ),
          ],
        ),
      );
}
