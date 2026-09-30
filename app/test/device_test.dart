import 'package:flutter_test/flutter_test.dart';
import 'package:oclock_companion/device.dart';

void main() {
  test('normalizes a device address without accepting a path', () {
    expect(DeviceApi.normalize('192.168.1.20:80').host, '192.168.1.20');
    expect(DeviceApi.normalize('192.168.1.20:80').path, '/');
    expect(
      () => DeviceApi.normalize('http://192.168.1.20/status'),
      throwsFormatException,
    );
  });

  test('parses the legacy console status and todos', () {
    const html = '''<form action="/wifi"></form>
      <input name="city" value="福州市">
      <form action="/todos/update"><input name="id" value="7"><input name="title" value="测试"><input name="due_date" value="2026-09-30"><select><option value="high" selected>高</option></select></form>
      <form action="/todos/complete"><input name="id" value="7"><input name="completed" value="0"></form>''';
    final snapshot = DeviceApi.parse({'mode': 'sta'}, html);
    expect(snapshot.city, '福州市');
    expect(snapshot.todos.single.completed, isTrue);
    expect(snapshot.todos.single.priority, 'high');
  });
}
