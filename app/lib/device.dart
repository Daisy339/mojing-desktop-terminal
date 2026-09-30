import 'dart:convert';

import 'package:html/parser.dart' as html;
import 'package:http/http.dart' as http;

class Todo {
  final String id, title, date, priority;
  final bool completed;
  Todo(this.id, this.title, this.date, this.priority, this.completed);
}

class DeviceSnapshot {
  final Map<String, dynamic> status;
  final List<Todo> todos;
  final String city;
  DeviceSnapshot(this.status, this.todos, this.city);
}

class DeviceApi {
  final Uri base;
  DeviceApi(String address) : base = normalize(address);
  static Uri normalize(String address) {
    final raw = address.trim();
    final uri = Uri.parse(raw.contains('://') ? raw : 'http://$raw');
    if (!['http', 'https'].contains(uri.scheme) ||
        uri.host.isEmpty ||
        uri.userInfo.isNotEmpty ||
        uri.hasQuery ||
        uri.hasFragment ||
        (uri.path.isNotEmpty && uri.path != '/')) {
      throw const FormatException('请输入设备 IP 或主机名，可带端口，不要填写接口路径');
    }
    return uri.replace(path: '/');
  }

  Future<DeviceSnapshot> load() async {
    final client = http.Client();
    try {
      final status = await client
          .get(base.resolve('/status'))
          .timeout(const Duration(seconds: 8));
      if (status.statusCode != 200) {
        throw Exception('读取状态失败：${status.statusCode}');
      }
      // Sequential requests avoid exhausting the embedded HTTP server sockets.
      final page = await client.get(base).timeout(const Duration(seconds: 12));
      if (page.statusCode != 200) throw Exception('读取控制台失败：${page.statusCode}');
      return parse(
        jsonDecode(utf8.decode(status.bodyBytes)) as Map<String, dynamic>,
        utf8.decode(page.bodyBytes),
      );
    } finally {
      client.close();
    }
  }

  static DeviceSnapshot parse(Map<String, dynamic> status, String source) {
    final doc = html.parse(source);
    if (doc.querySelector('form[action="/wifi"]') == null) {
      throw const FormatException('该地址不是兼容的 O’Clock 控制台');
    }
    final todos = <Todo>[];
    for (final form in doc.querySelectorAll('form[action="/todos/update"]')) {
      String value(String name) =>
          form.querySelector('[name="$name"]')?.attributes['value'] ?? '';
      final id = value('id');
      final complete =
          doc.querySelectorAll('form[action="/todos/complete"]').where(
                (f) =>
                    f.querySelector('[name="id"]')?.attributes['value'] == id,
              );
      todos.add(
        Todo(
          id,
          value('title'),
          value('due_date'),
          form.querySelector('option[selected]')?.attributes['value'] ??
              'medium',
          complete.isNotEmpty &&
              complete.first
                      .querySelector('[name="completed"]')
                      ?.attributes['value'] ==
                  '0',
        ),
      );
    }
    return DeviceSnapshot(
      status,
      todos,
      doc.querySelector('input[name="city"]')?.attributes['value'] ?? '',
    );
  }

  Future<void> post(
    String path,
    Map<String, dynamic> data, {
    bool json = false,
  }) async {
    final client = http.Client();
    try {
      final request = http.Request('POST', base.resolve(path))
        ..followRedirects = false;
      if (json) {
        request.headers['Content-Type'] = 'application/json';
        request.body = jsonEncode(data);
      } else {
        request.bodyFields = data.map((key, value) => MapEntry(key, '$value'));
        // Legacy firmware reads small form buffers; reject instead of silently truncating.
        final limit = path == '/wifi'
            ? 191
            : path == '/weather'
                ? 159
                : 255;
        if (request.bodyBytes.length > limit) {
          throw Exception('输入超过设备当前表单容量，请缩短内容');
        }
      }
      final response =
          await client.send(request).timeout(const Duration(seconds: 40));
      final body = await response.stream.bytesToString().timeout(
            const Duration(seconds: 10),
          );
      if (response.statusCode == 303) return;
      if (response.statusCode == 200 && body.trim() == 'ok') return;
      final message = html.parse(body).querySelector('p')?.text;
      throw Exception(message ?? '设备拒绝请求：${response.statusCode}');
    } finally {
      client.close();
    }
  }
}
