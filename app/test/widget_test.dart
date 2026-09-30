// This is a basic Flutter widget test.
//
// To perform an interaction with a widget in your test, use the WidgetTester
// utility in the flutter_test package. For example, you can send tap and scroll
// gestures. You can also use WidgetTester to find child widgets in the widget
// tree, read text, and verify that the values of widget properties are correct.

import 'package:flutter/material.dart';
import 'package:shared_preferences/shared_preferences.dart';
import 'package:flutter_test/flutter_test.dart';

import 'package:oclock_companion/main.dart';

void main() {
  testWidgets('Shows device connection form', (WidgetTester tester) async {
    // Use in-memory preferences; this does not contact a real device.
    SharedPreferences.setMockInitialValues({});
    await tester.pumpWidget(const OClockApp());
    await tester.pumpAndSettle();

    expect(find.text('ESP32 地址'), findsOneWidget);
    expect(find.text('连接'), findsOneWidget);

    // Dispose the periodic refresh timer before the test ends.
    await tester.pumpWidget(const SizedBox());

    await tester.pumpAndSettle();
  });
}
