import 'dart:async';
import 'dart:convert';
import 'package:flutter/material.dart';
import 'package:http/http.dart' as http;
import 'package:fl_chart/fl_chart.dart';
import 'package:intl/intl.dart';

void main() => runApp(const MyApp());

class MyApp extends StatelessWidget {
  const MyApp({super.key});
  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      debugShowCheckedModeBanner: false,
      title: 'Umidade do Solo',
      theme: ThemeData(primarySwatch: Colors.green),
      home: const DataPage(),
    );
  }
}

class DataPage extends StatefulWidget {
  const DataPage({super.key});
  @override
  State<DataPage> createState() => _DataPageState();
}

class _DataPageState extends State<DataPage> {
  /// Todas as linhas vindas da planilha (em ordem)
  List<List<dynamic>> _todas = [];

  /// Quantidade de pontos visíveis
  final List<int> _opcoesQtd = const [10, 20, 30, 40, 50, 100, 200, 500, 1000];
  int _qtdVisivel = 10;

  Timer? _timer;

  // --- Calibração (relação inversa: ADC alto = mais seco => % menor) ---
  static const double _adcMin = 500;   // 100% (úmido)
  static const double _adcMax = 3000;  //   0% (seco)

  // formatadores
  final _fmtHora = DateFormat('HH:mm');
  final _fmtDataHoraBR = DateFormat('dd/MM/yyyy HH:mm');
  final _fmtISO = DateFormat('yyyy-MM-dd HH:mm:ss');

  // Converte ADC para % (invertido)
  double _adcToPercent(double adc) {
    final pct = ((_adcMax - adc) / (_adcMax - _adcMin)) * 100.0;
    return pct.clamp(0.0, 100.0);
  }

  /// Fatia as últimas N linhas, conforme dropdown
  List<List<dynamic>> get _visiveis {
    if (_todas.isEmpty) return const [];
    final n = _qtdVisivel;
    if (_todas.length <= n) return List<List<dynamic>>.from(_todas);
    return _todas.sublist(_todas.length - n);
  }

  Future<void> carregarDados() async {
    try {
      const planilhaId = "Copie_e_cole_ID_da_Planilha_Aqui"; // <- seu ID
      final url =
          "https://docs.google.com/spreadsheets/d/$planilhaId/gviz/tq?tqx=out:json";

      final resp = await http.get(Uri.parse(url));
      if (resp.statusCode != 200) throw Exception("HTTP ${resp.statusCode}");

      final body = resp.body;
      final start = body.indexOf('{');
      final end = body.lastIndexOf('}');
      if (start == -1 || end == -1 || end <= start) {
        throw Exception("Formato inesperado da resposta do Sheets");
      }
      final jsonStr = body.substring(start, end + 1);
      final jsonData = json.decode(jsonStr);

      final rowsJson = (jsonData["table"]?["rows"] as List?) ?? [];
      final rows = <List<dynamic>>[];

      // prioriza "v" (valor cru; datas em Date(...)). Se nulo, usa "f".
      for (final r in rowsJson) {
        final cells = (r["c"] as List?) ?? [];
        final linha = <dynamic>[];
        for (final c in cells) {
          if (c == null) {
            linha.add("");
          } else {
            linha.add(c["v"] ?? c["f"]);
          }
        }
        rows.add(linha);
      }

      if (!mounted) return;
      setState(() => _todas = rows);
    } catch (e) {
      if (!mounted) return;
      ScaffoldMessenger.of(context)
          .showSnackBar(SnackBar(content: Text("Falha ao carregar dados: $e")));
    }
  }

  @override
  void initState() {
    super.initState();
    carregarDados();
    _timer = Timer.periodic(const Duration(seconds: 30), (_) => carregarDados());
  }

  @override
  void dispose() {
    _timer?.cancel();
    super.dispose();
  }

  // ---------- Parse helpers ----------

  DateTime? _parseAnyDate(dynamic raw) {
    if (raw == null) return null;

    if (raw is DateTime) return raw;

    if (raw is num) {
      if (raw > 1000000000000) return DateTime.fromMillisecondsSinceEpoch(raw.toInt());
      if (raw > 1000000000)     return DateTime.fromMillisecondsSinceEpoch(raw.toInt() * 1000);
    }

    if (raw is String) {
      // ISO
      final iso = DateTime.tryParse(raw);
      if (iso != null) return iso;

      // GViz: Date(yyyy,mm,dd,HH,MM,SS)
      final reGviz = RegExp(r'^Date\((\d+),(\d+),(\d+)(?:,(\d+),(\d+),(\d+))?\)$');
      final mg = reGviz.firstMatch(raw);
      if (mg != null) {
        final y  = int.parse(mg.group(1)!);
        final m0 = int.parse(mg.group(2)!); // 0-based
        final d  = int.parse(mg.group(3)!);
        final hh = int.tryParse(mg.group(4) ?? '0') ?? 0;
        final mm = int.tryParse(mg.group(5) ?? '0') ?? 0;
        final ss = int.tryParse(mg.group(6) ?? '0') ?? 0;
        return DateTime(y, m0 + 1, d, hh, mm, ss);
      }

      // pt-BR: dd/MM/yyyy HH:mm[:ss]
      try { return DateFormat('dd/MM/yyyy HH:mm:ss').parseStrict(raw); } catch (_) {}
      try { return DateFormat('dd/MM/yyyy HH:mm').parseStrict(raw); } catch (_) {}

      // fallback (se já vier nesse formato)
      try { return _fmtISO.parseStrict(raw); } catch (_) {}
    }

    return null;
  }

  double? _parseAdc(dynamic v) {
    if (v == null) return null;
    if (v is num) return v.toDouble();
    final s = v.toString().trim().replaceAll(',', '.');
    return double.tryParse(s);
  }

  // ---------- Dados do gráfico (0–100%) ----------

  List<FlSpot> _spotsPercent() {
    final vis = _visiveis;
    final spots = <FlSpot>[];
    for (int i = 0; i < vis.length; i++) {
      final adc = _parseAdc(vis[i].length > 1 ? vis[i][1] : null);
      if (adc != null) {
        final pct = _adcToPercent(adc);
        spots.add(FlSpot(i.toDouble(), pct));
      }
    }
    return spots;
  }

  // Rótulos no eixo X: só HH:mm, em vertical, com fallback
  SideTitles _bottomTitles() {
    final vis = _visiveis;
    return SideTitles(
      showTitles: true,
      reservedSize: 48,
      interval: 1,
      getTitlesWidget: (value, meta) {
        final idx = value.toInt();
        if (idx < 0 || idx >= vis.length) return const SizedBox.shrink();

        String label = "";
        final dt = _parseAnyDate(vis[idx][0]);
        if (dt != null) {
          label = _fmtHora.format(dt); // HH:mm
        } else {
          final raw = vis[idx][0]?.toString() ?? "";
          final m = RegExp(r'(\d{2}:\d{2})').firstMatch(raw);
          if (m != null) label = m.group(1)!;
        }
        if (label.isEmpty) return const SizedBox.shrink();

        return Padding(
          padding: const EdgeInsets.only(top: 8),
          child: RotatedBox(
            quarterTurns: 3, // vertical
            child: Text(label, style: const TextStyle(fontSize: 11)),
          ),
        );
      },
    );
  }

  LineChartData _chartData() {
    final spots = _spotsPercent();
    return LineChartData(
      minX: 0,
      maxX: spots.isNotEmpty ? (spots.length - 1).toDouble() : 1,
      minY: 0,          // %
      maxY: 100,        // %
      titlesData: FlTitlesData(
        bottomTitles: AxisTitles(sideTitles: _bottomTitles()),
        leftTitles: AxisTitles(
          sideTitles: SideTitles(
            showTitles: true,
            reservedSize: 48,
            interval: 20,
            getTitlesWidget: (v, m) =>
                Text("${v.toInt()}%", style: const TextStyle(fontSize: 11)),
          ),
        ),
        rightTitles: const AxisTitles(sideTitles: SideTitles(showTitles: false)),
        topTitles: const AxisTitles(sideTitles: SideTitles(showTitles: false)),
      ),
      gridData: const FlGridData(show: true),
      borderData: FlBorderData(
        show: true,
        border: const Border(
          top: BorderSide(color: Colors.black12),
          right: BorderSide(color: Colors.black12),
          left: BorderSide(color: Colors.black12),
          bottom: BorderSide(color: Colors.black12),
        ),
      ),
      lineBarsData: [
        LineChartBarData(
          spots: spots,
          isCurved: true,
          dotData: const FlDotData(show: false),
          belowBarData: BarAreaData(show: false),
        ),
      ],
    );
  }

  @override
  Widget build(BuildContext context) {
    final vis = _visiveis;

    return Scaffold(
      appBar: AppBar(
        backgroundColor: Colors.blueGrey,
        title: const Text("Umidade do Solo"),
        actions: [
          // Dropdown na AppBar
          Padding(
            padding: const EdgeInsets.only(right: 8),
            child: PopupMenuButton<int>(
              initialValue: _qtdVisivel,
              onSelected: (v) {
                setState(() => _qtdVisivel = v);
              },
              itemBuilder: (_) => _opcoesQtd
                  .map((n) => PopupMenuItem<int>(
                        value: n,
                        child: Text("$n últimos"),
                      ))
                  .toList(),
              child: Container(
                padding:
                    const EdgeInsets.symmetric(horizontal: 12, vertical: 8),
                decoration: BoxDecoration(
                  color: Colors.white24,
                  borderRadius: BorderRadius.circular(8),
                  border: Border.all(color: Colors.black54),
                ),
                child: Row(
                  mainAxisSize: MainAxisSize.min,
                  children: [
                    Text("$_qtdVisivel últimos",
                        style: const TextStyle(
                            color: Colors.black, fontWeight: FontWeight.w600)),
                    const SizedBox(width: 6),
                    const Icon(Icons.arrow_drop_down, color: Colors.black),
                  ],
                ),
              ),
            ),
          ),
        ],
      ),
      body: RefreshIndicator(
        onRefresh: carregarDados,
        child: vis.isEmpty
            ? ListView(
                children: [
                  SizedBox(height: 400, child: Center(child: CircularProgressIndicator())),
                ],
              )
            : ListView(
                children: [
                  // Gráfico
                  Padding(
                    padding: const EdgeInsets.fromLTRB(12, 12, 12, 4),
                    child: Card(
                      elevation: 2,
                      child: Padding(
                        padding: const EdgeInsets.all(12),
                        child: SizedBox(height: 260, child: LineChart(_chartData())),
                      ),
                    ),
                  ),
                  // Lista (DATA + HORA + ADC e %)
                  ListView.builder(
                    shrinkWrap: true,
                    physics: const NeverScrollableScrollPhysics(),
                    reverse: true, // mais recente no topo
                    itemCount: vis.length,
                    itemBuilder: (context, index) {
                      final item = vis[index];
                      final dt = _parseAnyDate(item[0]);
                      final dataHoraLabel = dt != null
                          ? _fmtDataHoraBR.format(dt)
                          : (item[0]?.toString() ?? "");
                      final adcVal = _parseAdc(item.length > 1 ? item[1] : null);
                      final adcInt = adcVal?.toInt();
                      final pct = adcVal != null ? _adcToPercent(adcVal).toStringAsFixed(0) : null;

                      return Card(
                        margin: const EdgeInsets.symmetric(horizontal: 12, vertical: 6),
                        child: ListTile(
                          leading: const Icon(Icons.water_drop, color: Colors.blue),
                          title: Text("Data/Hora: $dataHoraLabel"),
                          subtitle: Text(
                            "Valor ADC: ${adcInt ?? '-'}   •   Umidade: ${pct != null ? "$pct%" : '-'}",
                          ),
                        ),
                      );
                    },
                  ),
                  const SizedBox(height: 12),
                ],
              ),
      ),
    );
  }
}
