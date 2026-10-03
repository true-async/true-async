#!/usr/bin/env python3
"""Render the progress page of dev/PLAN.md as one HTML file for a claude.ai artifact.

    plan-page.py [--out FILE] [--ref REV]

The page shows the overall progress, every stage with its steps and status, the active stage in
detail (with the count of list tests each step still owes, read from their --XFAIL-- sections), the
fog and the latest commits of REV (HEAD by default). It writes to FILE, or to stdout without --out.
The artifact wraps the page in its own document skeleton, so the output has no <html> or <body>.

Progress figures come from tools/roadmap.py, so the page and the README roadmap always agree. The
labels are Russian: the page is read by the project owner in Russian; plan text is quoted as is.
"""
import argparse
import html
import importlib.util
import re
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path

TOOLS = Path(__file__).resolve().parent
ROOT = TOOLS.parent
PLAN = ROOT / 'dev' / 'PLAN.md'
REPO_URL = 'https://github.com/true-async/true-async'
COMMITS = 8
FONTS = ('https://fonts.googleapis.com/css2?family=Golos+Text:wght@400;500;600;700'
         '&family=JetBrains+Mono:wght@400;600&family=Unbounded:wght@500;700&display=swap')


def load_tool(name):
    """Import a sibling tool by file name: `plan-page.py` itself is not a valid module name."""
    spec = importlib.util.spec_from_file_location(name, TOOLS / f'{name}.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)

    return module


roadmap = load_tool('roadmap')
lists = load_tool('lists')

STAGE_HEAD = re.compile(r'^## (S\d+) — (.+?)\s+\[([^\]]*)\](.*)$', re.M)
STEP_HEAD = re.compile(r'^- \[(.)\] (S\d+\.\d+) (.*)$')
# Fields inside a step: `done:`, `tier:`, `handoff:`, `Notes:`, or a dated record such as
# "Critic 2026-10-01:" and "Core update 2026-10-02:".
STEP_FIELD = re.compile(r'^(done|tier|handoff|Notes|[A-Z][a-z]+(?: [a-z]+)? \d{4}-\d{2}-\d{2}):\s*(.*)$')
STAGE_FIELD = re.compile(r'^(Goal|Done when|Tier|Trees|Test ownership|Notes|Base):\s*(.*)$')
XFAIL_STEP = re.compile(rb'^--XFAIL--\r?\n[^\n]*?(S\d+\.\d+)', re.M)

STATUS_LABEL = {'done': 'готово', 'active': 'в работе', 'todo': 'впереди', 'deferred': 'отложено'}
FIELD_LABEL = {'done': 'Готово, когда', 'handoff': 'Итог', 'Notes': 'Заметки'}


def inline(text):
    """Escape plan text and render its `code` and **bold** spans."""
    text = html.escape(text, quote=False)
    text = re.sub(r'`([^`]+)`', r'<code>\1</code>', text)

    return re.sub(r'\*\*([^*]+)\*\*', r'<b>\1</b>', text)


def split_fields(lines, pattern):
    """(lead text, [(key, text)]) of indented plan lines; a field runs until the next one."""
    lead, fields = [], []

    for line in lines:
        match = pattern.match(line.strip())

        if match:
            fields.append([match.group(1), match.group(2)])
        elif fields:
            fields[-1][1] += ' ' + line.strip()
        else:
            lead.append(line.strip())

    return ' '.join(lead).strip(), [(key, text.strip()) for key, text in fields]


def parse_steps(body):
    """Steps of one stage body: dicts with id, mark, summary and fields, in plan order."""
    steps, current = [], None

    for line in body.splitlines():
        head = STEP_HEAD.match(line)

        if head:
            current = {'mark': head.group(1), 'id': head.group(2), 'lines': [head.group(3)]}
            steps.append(current)
        elif current and line.startswith('      '):
            current['lines'].append(line)
        else:
            current = None

    for step in steps:
        step['summary'], step['fields'] = split_fields(step.pop('lines'), STEP_FIELD)

    return steps


def parse_plan(plan):
    """Plan header facts and the stages with their steps and statuses."""
    updated = re.search(r'^Updated:\s*(\S+)', plan, re.M)
    active = re.search(r'Active:\s*(S\d+\.\d+)', plan)
    active_step = active.group(1) if active else None
    progress = {number: value for number, _, value in roadmap.stages(plan)}
    heads = list(STAGE_HEAD.finditer(plan))
    stages = []

    for i, head in enumerate(heads):
        end = heads[i + 1].start() if i + 1 < len(heads) else len(plan)
        body = plan[head.end():end].split('\n## ', 1)[0]
        intro = body.split('\n- [', 1)[0].strip().splitlines()
        lead, fields = split_fields(intro, STAGE_FIELD)
        steps = parse_steps(body)
        number = head.group(1)

        for step in steps:
            if step['mark'] == 'x':
                step['status'] = 'done'
            elif step['id'] == active_step:
                step['status'] = 'active'
            elif 'Deferred' in step['summary']:
                step['status'] = 'deferred'
            else:
                step['status'] = 'todo'

        mark = head.group(3).strip()

        if mark.startswith(('x', 'done')):
            status = 'done'
        elif active_step and active_step.startswith(number + '.'):
            status = 'active'
        else:
            status = 'todo'

        stages.append({
            'number': number, 'title': head.group(2), 'note': head.group(4).strip(' ()'),
            'status': status, 'progress': progress.get(number, 0.0), 'steps': steps,
            'goal': dict(fields).get('Goal') or lead, 'done_when': dict(fields).get('Done when'),
            'tier': dict(fields).get('Tier'),
        })

    fog = re.search(r'^## Fog\n(.*?)(?=^## )', plan, re.M | re.S)
    fog_items = re.split(r'\n(?=- )', fog.group(1).strip()) if fog else []

    return {
        'updated': updated.group(1) if updated else '?',
        'active': active_step,
        'stages': stages,
        'fog': [' '.join(item[2:].split()) for item in fog_items if item.startswith('- ')],
    }


def owed_tests(stage_number):
    """(tests listed for the stage, {step: tests whose --XFAIL-- names it}) from tests/lists/."""
    list_file = lists.LISTS / f'{stage_number}.txt'

    if not list_file.exists():
        return 0, {}

    entries = lists.read(list_file)
    owed = {}

    for entry in entries:
        path = ROOT / 'tests' / entry.path

        if not path.exists():
            continue

        match = XFAIL_STEP.search(path.read_bytes())

        if match:
            step = match.group(1).decode()
            owed[step] = owed.get(step, 0) + 1

    return len(entries), owed


def recent_commits(ref):
    """[(short hash, date, subject)] of the latest commits of `ref`; empty outside a git checkout."""
    try:
        out = subprocess.run(
            ['git', '-C', str(ROOT), 'log', f'-n{COMMITS}', '--format=%h%x09%as%x09%s', '--end-of-options', ref],
            capture_output=True, text=True, check=True).stdout
    except (OSError, subprocess.CalledProcessError):
        return []

    return [tuple(line.split('\t', 2)) for line in out.splitlines() if line.count('\t') >= 2]


def tests_waiting(count):
    """Russian "N tests are waiting" with the noun and verb agreeing with N."""
    if count % 10 == 1 and count % 100 != 11:
        return f'{count} тест ждёт'

    if 2 <= count % 10 <= 4 and not 12 <= count % 100 <= 14:
        return f'{count} теста ждут'

    return f'{count} тестов ждут'


def tier_chips(text):
    """Chips for a step's `tier:` field, "T2 · role: Critic" → T2, Critic."""
    chips = []

    for part in (text or '').split('·'):
        part = part.strip()
        value = part.split(':', 1)[1].strip() if part.startswith('role:') else part

        if value and value != '—':
            chips.append(f'<span class="chip">{inline(value)}</span>')

    return ''.join(chips)


def render_step(step, owed, open_details):
    fields = dict(step['fields'])
    rows = []
    owes = owed.get(step['id'], 0)

    if fields.get('done') and step['status'] != 'done':
        rows.append(f'<p class="field"><span class="key">Готово, когда</span> {inline(fields["done"])}</p>')

    if fields.get('handoff'):
        rows.append(f'<p class="field"><span class="key">Итог</span> {inline(fields["handoff"])}</p>')

    history = [(key, text) for key, text in step['fields'] if key not in ('done', 'tier', 'handoff')]

    if history:
        items = ''.join(f'<li><span class="key">{inline(key)}</span> {inline(text)}</li>' for key, text in history)
        rows.append(f'<details{" open" if open_details else ""}><summary>История шага ({len(history)})</summary>'
                    f'<ul class="history">{items}</ul></details>')

    owe_chip = f'<span class="chip owe">{tests_waiting(owes)}</span>' if owes else ''

    return (f'<li class="step {step["status"]}"><div class="mark" aria-hidden="true"></div><div class="step-body">'
            f'<div class="step-head"><span class="step-id">{step["id"]}</span>'
            f'<span class="pill {step["status"]}">{STATUS_LABEL[step["status"]]}</span>'
            f'{tier_chips(fields.get("tier"))}{owe_chip}</div>'
            f'<p>{inline(step["summary"])}</p>{"".join(rows)}</div></li>')


def render_stage_tile(stage):
    percent = round(stage['progress'] * 100)
    steps = stage['steps']
    done = sum(1 for step in steps if step['status'] == 'done')
    count = f'{done} из {len(steps)} шагов' if steps else 'шаги не расписаны'

    return (f'<a class="tile {stage["status"]}" href="#{stage["number"].lower()}">'
            f'<span class="tile-num">{stage["number"]}</span>'
            f'<span class="tile-title">{inline(stage["title"])}</span>'
            f'<span class="bar"><span style="width:{percent}%"></span></span>'
            f'<span class="tile-meta"><span>{percent} %</span><span>{count}</span></span></a>')


def render_stage(stage, owed):
    active = stage['status'] == 'active'
    steps = ''.join(render_step(step, owed if active else {}, False) for step in stage['steps'])
    note = f' <span class="note">({inline(stage["note"])})</span>' if stage['note'] else ''
    parts = []

    if stage['goal']:
        parts.append(f'<p><span class="key">Цель</span> {inline(stage["goal"])}</p>')

    if stage['done_when']:
        parts.append(f'<p><span class="key">Готово, когда</span> {inline(stage["done_when"])}</p>')

    if steps:
        parts.append(f'<ol class="steps">{steps}</ol>')

    return (f'<details class="stage {stage["status"]}" id="{stage["number"].lower()}"{" open" if active else ""}>'
            f'<summary><span class="stage-num">{stage["number"]}</span>'
            f'<span class="stage-title">{inline(stage["title"])}{note}</span>'
            f'<span class="pill {stage["status"]}">{STATUS_LABEL[stage["status"]]}</span>'
            f'<span class="stage-pct">{round(stage["progress"] * 100)} %</span></summary>'
            f'<div class="stage-body">{"".join(parts)}</div></details>')


def render(data, owed_total, owed, commits, generated):
    stages = data['stages']
    total = round(sum(stage['progress'] for stage in stages) / len(stages) * 100)
    active_stage = next((stage for stage in stages if stage['status'] == 'active'), None)
    active_step = None

    if active_stage:
        active_step = next((step for step in active_stage['steps'] if step['id'] == data['active']), None)

    waiting = sum(owed.values())
    stage_done = sum(1 for step in active_stage['steps'] if step['status'] == 'done') if active_stage else 0

    summary = [
        f'<div class="stat"><span class="stat-value">{total} %</span>'
        f'<span class="stat-label">весь план, среднее по {len(stages)} этапам</span></div>',
    ]

    if active_stage:
        summary.append(
            f'<div class="stat"><span class="stat-value">{stage_done} / {len(active_stage["steps"])}</span>'
            f'<span class="stat-label">шагов {active_stage["number"]} закрыто</span></div>')

    if owed_total:
        summary.append(
            f'<div class="stat"><span class="stat-value">{owed_total - waiting} / {owed_total}</span>'
            f'<span class="stat-label">тестов списка {active_stage["number"]} без XFAIL, CI требует PASS</span></div>')

    now = ''

    if active_step:
        now = (f'<section class="now" aria-labelledby="now-h"><div class="eyebrow" id="now-h">Сейчас в работе</div>'
               f'<h2>{active_step["id"]} · {inline(active_stage["title"])}</h2>'
               f'<p>{inline(active_step["summary"])}</p>')
        done_when = dict(active_step['fields']).get('done')

        if done_when:
            now += f'<p><span class="key">Готово, когда</span> {inline(done_when)}</p>'

        upcoming = [step for step in active_stage['steps'] if step['status'] == 'todo']

        if upcoming:
            step = upcoming[0]
            now += f'<p class="next"><span class="key">Дальше</span> {step["id"]}: {inline(step["summary"])}</p>'

        now += '</section>'

    fog = ''.join(f'<li>{inline(item)}</li>' for item in data['fog'])
    commit_rows = ''.join(
        f'<tr><td class="mono"><a href="{REPO_URL}/commit/{sha}">{sha}</a></td>'
        f'<td class="mono date">{date}</td><td>{inline(subject)}</td></tr>' for sha, date, subject in commits)

    return f'''<title>TrueAsync: прогресс</title>
<link rel="preconnect" href="https://fonts.googleapis.com">
<link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>
<link rel="stylesheet" href="{FONTS}">
<style>
/* Layout: one reading column; summary figures, the active step, a wrapping strip of stage tiles
   (the order is the plan's), then every stage as a collapsible block, fog and latest commits. */
:root {{
  --bg: #f6f7f4; --surface: #ffffff; --ink: #1c2420; --muted: #5e6b64; --line: #d9dfd9;
  --accent: #1f6f5c; --accent-soft: #e2efe9; --warn: #9a5b00; --warn-soft: #fbf0dc; --chip: #eef1ec;
  --font-display: "Unbounded", "Golos Text", system-ui, sans-serif;
  --font-body: "Golos Text", system-ui, -apple-system, "Segoe UI", sans-serif;
  --font-mono: "JetBrains Mono", ui-monospace, "SFMono-Regular", Menlo, monospace;
}}
@media (prefers-color-scheme: dark) {{
  :root:not([data-theme="light"]) {{
    --bg: #121714; --surface: #1a211d; --ink: #e6ece8; --muted: #9aa8a0; --line: #2c3631;
    --accent: #5cc4a6; --accent-soft: #1d3a31; --warn: #e8b061; --warn-soft: #3a2c15; --chip: #232c27;
    color-scheme: dark;
  }}
}}
:root[data-theme="dark"] {{
  --bg: #121714; --surface: #1a211d; --ink: #e6ece8; --muted: #9aa8a0; --line: #2c3631;
  --accent: #5cc4a6; --accent-soft: #1d3a31; --warn: #e8b061; --warn-soft: #3a2c15; --chip: #232c27;
  color-scheme: dark;
}}
* {{ box-sizing: border-box; }}
body {{ background: var(--bg); color: var(--ink); font: 15px/1.6 var(--font-body); }}
.wrap {{ max-width: 960px; margin: 0 auto; padding: 32px 16px 64px; display: grid; gap: 36px; }}
h1, h2 {{ font-family: var(--font-display); font-weight: 700; text-wrap: balance; margin: 0; letter-spacing: -0.01em; }}
h1 {{ font-size: clamp(26px, 5vw, 36px); line-height: 1.15; }}
h2 {{ font-size: 19px; line-height: 1.3; }}
p {{ margin: 0; max-width: 75ch; overflow-wrap: anywhere; }}
a {{ color: var(--accent); }}
a:focus-visible, summary:focus-visible {{ outline: 2px solid var(--accent); outline-offset: 2px; }}
code, .mono {{ font-family: var(--font-mono); font-size: 0.86em; }}
.eyebrow {{ font: 600 12px/1.4 var(--font-body); letter-spacing: 0.08em; text-transform: uppercase;
  color: var(--accent); }}
.lead {{ color: var(--muted); }}
.key {{ font-weight: 600; color: var(--muted); margin-right: 4px; }}
header, section {{ display: grid; gap: 14px; min-width: 0; }}

.stats {{ display: grid; grid-template-columns: repeat(auto-fit, minmax(200px, 1fr)); gap: 10px; }}
.stat {{ background: var(--surface); border: 1px solid var(--line); border-radius: 8px; padding: 14px 16px;
  display: grid; gap: 4px; }}
.stat-value {{ font: 700 26px/1.1 var(--font-display); font-variant-numeric: tabular-nums; }}
.stat-label {{ color: var(--muted); font-size: 13px; }}

.now {{ background: var(--accent-soft); border: 1px solid var(--accent); border-radius: 10px; padding: 18px 20px;
  gap: 10px; }}
.now .next {{ color: var(--muted); }}

.tiles {{ display: grid; grid-template-columns: repeat(auto-fill, minmax(170px, 1fr)); gap: 8px; }}
.tile {{ display: grid; gap: 6px; padding: 12px; border-radius: 8px; border: 1px solid var(--line);
  background: var(--surface);
  color: var(--ink); text-decoration: none; min-width: 0; }}
.tile:hover {{ border-color: var(--accent); }}
.tile.active {{ border-color: var(--accent); box-shadow: inset 0 0 0 1px var(--accent); }}
.tile-num {{ font: 700 13px/1 var(--font-display); color: var(--accent); }}
.tile.todo .tile-num {{ color: var(--muted); }}
.tile-title {{ font-size: 13px; line-height: 1.35; font-weight: 500; }}
.tile-meta {{ display: flex; justify-content: space-between; gap: 6px; font-size: 12px; color: var(--muted);
  font-variant-numeric: tabular-nums; }}
.bar {{ height: 6px; border-radius: 3px; background: var(--chip); overflow: hidden; }}
.bar span {{ display: block; height: 100%; background: var(--accent); }}

.stages {{ display: grid; gap: 8px; }}
.stage {{ background: var(--surface); border: 1px solid var(--line); border-radius: 8px; min-width: 0; }}
.stage.active {{ border-color: var(--accent); }}
.stage > summary {{ display: flex; flex-wrap: wrap; align-items: center; gap: 8px 12px; padding: 12px 16px;
  cursor: pointer; list-style: none; }}
.stage > summary::-webkit-details-marker {{ display: none; }}
.stage > summary::before {{ content: "▸"; color: var(--muted); transition: transform .15s; }}
.stage[open] > summary::before {{ transform: rotate(90deg); }}
.stage-num {{ font: 700 14px/1 var(--font-display); color: var(--accent); min-width: 34px; }}
.stage.todo .stage-num {{ color: var(--muted); }}
.stage-title {{ font-weight: 600; flex: 1 1 240px; min-width: 0; }}
.stage-title .note {{ font-weight: 400; color: var(--muted); font-size: 13px; }}
.stage-pct {{ font-family: var(--font-mono); font-size: 13px; color: var(--muted);
  font-variant-numeric: tabular-nums; }}
.stage-body {{ padding: 0 16px 16px; display: grid; gap: 10px; border-top: 1px solid var(--line); padding-top: 12px; }}

.pill {{ font: 600 11px/1 var(--font-body); letter-spacing: 0.04em; text-transform: uppercase; padding: 5px 8px;
  border-radius: 999px;
  background: var(--chip); color: var(--muted); white-space: nowrap; }}
.pill.done {{ background: var(--accent-soft); color: var(--accent); }}
.pill.active {{ background: var(--accent); color: var(--surface); }}
.pill.deferred {{ background: var(--warn-soft); color: var(--warn); }}
.chip {{ font: 500 12px/1 var(--font-mono); background: var(--chip); border-radius: 999px; padding: 5px 9px;
  white-space: nowrap; }}
.chip.owe {{ background: var(--warn-soft); color: var(--warn); }}

.steps {{ list-style: none; margin: 0; padding: 0; display: grid; }}
.step {{ display: grid; grid-template-columns: 22px 1fr; gap: 12px; }}
.step .mark {{ display: flex; flex-direction: column; align-items: center; }}
.step .mark::before {{ content: ""; width: 12px; height: 12px; border-radius: 50%; margin-top: 6px; flex: none;
  border: 2px solid var(--muted); background: var(--surface); }}
.step .mark::after {{ content: ""; flex: 1; width: 2px; background: var(--line); margin-top: 4px; }}
.step:last-child .mark::after {{ display: none; }}
.step.done .mark::before {{ background: var(--accent); border-color: var(--accent); }}
.step.active .mark::before {{ border-color: var(--accent); box-shadow: 0 0 0 4px var(--accent-soft); }}
.step.deferred .mark::before {{ border-color: var(--warn); border-style: dashed; }}
.step-body {{ display: grid; gap: 6px; padding-bottom: 16px; min-width: 0; font-size: 14px; }}
.step.done .step-body > p:first-of-type {{ color: var(--muted); }}
.step-head {{ display: flex; flex-wrap: wrap; align-items: center; gap: 6px; }}
.step-id {{ font: 700 13px/1 var(--font-mono); margin-right: 4px; }}
.field {{ font-size: 13px; }}
details > summary {{ cursor: pointer; }}
.step details summary {{ color: var(--muted); font-size: 13px; }}
.history {{ margin: 6px 0 0; padding-left: 18px; display: grid; gap: 6px; font-size: 13px; }}
.history li {{ overflow-wrap: anywhere; }}

.fog {{ margin: 0; padding-left: 18px; display: grid; gap: 6px; font-size: 14px; }}
.tablebox {{ overflow-x: auto; border: 1px solid var(--line); border-radius: 8px; background: var(--surface); }}
table {{ border-collapse: collapse; width: 100%; font-size: 14px; }}
td {{ padding: 8px 12px; border-bottom: 1px solid var(--line); vertical-align: top; }}
tr:last-child td {{ border-bottom: 0; }}
td.date {{ color: var(--muted); white-space: nowrap; }}
footer {{ color: var(--muted); font-size: 13px; }}
@media (prefers-reduced-motion: reduce) {{ * {{ transition: none !important; }} }}
@media (max-width: 520px) {{ .stat-value {{ font-size: 22px; }} .stage > summary {{ padding: 12px;
  }} .stage-body {{ padding: 12px; }} }}
</style>

<div class="wrap">
  <header>
    <div class="eyebrow">План от {data["updated"]} · страница собрана {generated}</div>
    <h1>TrueAsync: прогресс</h1>
    <p class="lead">Сводка по <code>dev/PLAN.md</code> из ветки main репозитория
      <a href="{REPO_URL}">true-async/true-async</a>. Страница пересобирается каждое утро, тексты шагов
      приведены как в плане.</p>
    <div class="stats">{"".join(summary)}</div>
  </header>
  {now}
  <section aria-labelledby="tiles-h">
    <h2 id="tiles-h">Этапы</h2>
    <nav class="tiles">{"".join(render_stage_tile(stage) for stage in stages)}</nav>
  </section>
  <section aria-labelledby="detail-h">
    <h2 id="detail-h">Шаги по этапам</h2>
    <div class="stages">{"".join(render_stage(stage, owed) for stage in stages)}</div>
  </section>
  <section aria-labelledby="fog-h">
    <h2 id="fog-h">Туман</h2>
    <ul class="fog">{fog}</ul>
  </section>
  <section aria-labelledby="git-h">
    <h2 id="git-h">Последние коммиты в main</h2>
    <div class="tablebox"><table><tbody>{commit_rows}</tbody></table></div>
  </section>
  <footer>Процент этапа: закрытые шаги к расписанным, как в README (<code>tools/roadmap.py</code>).
    Тест «ждёт» шаг, если его секция <code>--XFAIL--</code> называет этот шаг.
    Собрано <code>tools/plan-page.py</code>.</footer>
</div>
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('--out', type=Path, help='write the page here instead of stdout')
    parser.add_argument('--ref', default='HEAD', help='revision whose latest commits are listed')
    args = parser.parse_args()

    data = parse_plan(PLAN.read_text())
    active = next((stage['number'] for stage in data['stages'] if stage['status'] == 'active'), None)
    owed_total, owed = owed_tests(active) if active else (0, {})
    generated = datetime.now(timezone.utc).strftime('%Y-%m-%d %H:%M UTC')
    page = render(data, owed_total, owed, recent_commits(args.ref), generated)

    if args.out:
        args.out.write_text(page)
    else:
        sys.stdout.write(page)

    return 0


if __name__ == '__main__':
    sys.exit(main())
