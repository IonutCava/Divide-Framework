// Copyright (c) 2026 DIVIDE-Studio
const assert = require('node:assert/strict');
const test = require('node:test');
const schedule = require('./daily-ci.cjs');
const { readFileSync } = require('node:fs');
const path = require('node:path');

function fixture({ pulls = [], statuses = {}, failDispatch } = {}) {
    const dispatched = [];
    const reported = [];
    const errors = [];
    const failures = [];
    const github = {
        rest: {
            repos: {
                get: async () => ({ data: { default_branch: 'master' } }),
                getBranch: async ({ branch }) => {
                    assert.equal(branch, 'master');
                    return { data: { commit: { sha: 'master-sha' } } };
                },
                listCommitStatusesForRef: 'statuses',
                createCommitStatus: async status => reported.push(status)
            },
            pulls: { list: 'pulls' },
            actions: {
                createWorkflowDispatch: async dispatch => {
                    if (failDispatch?.(dispatch)) {
                        throw new Error('Dispatch unavailable');
                    }
                    dispatched.push(dispatch);
                }
            }
        },
        paginate: async (method, options) => {
            assert.equal(options.per_page, 100);
            return method === 'pulls' ? pulls : (statuses[options.ref] || []);
        }
    };
    return {
        args: {
            github,
            context: {
                repo: { owner: 'owner', repo: 'repo' },
                serverUrl: 'https://github.com',
                runId: 123
            },
            core: {
                info: () => {},
                error: message => errors.push(message),
                setFailed: message => failures.push(message)
            }
        },
        dispatched, reported, errors, failures
    };
}

function prior(target, state = 'success') {
    return ['linux', 'windows', 'macos'].map(platform => ({
        context: `daily-ci/${platform}/${target}`, state,
        description: 'Daily build passed'
    }));
}

test('first daily check builds master and every open PR, including fork heads', async () => {
    const f = fixture({
        pulls: [
            { number: 1, head: { sha: 'fork-sha', repo: { fork: true } } },
            { number: 2, head: { sha: 'other-sha' } }
        ]
    });
    await schedule(f.args);
    assert.equal(f.dispatched.length, 9);
    assert.equal(f.reported.length, 9);
    for (const dispatch of f.dispatched) {
        assert.equal(dispatch.ref, 'master');
        assert.ok(['Linux.yml', 'Windows.yml', 'MacOS.yml'].includes(dispatch.workflow_id));
    }
    assert.deepEqual(f.dispatched[3].inputs, { ci_sha: 'fork-sha', ci_target: 'pr-1' });
    assert.ok(f.reported.every(status => status.state === 'pending'));
});

test('unchanged heads are skipped regardless of previous build result', async () => {
    for (const state of ['pending', 'success', 'failure', 'error']) {
        const f = fixture({ statuses: { 'master-sha': prior('master', state) } });
        await schedule(f.args);
        assert.equal(f.dispatched.length, 0);
        assert.equal(f.reported.length, 0);
    }
});

test('only new PR commits run; commit age does not matter', async () => {
    const f = fixture({
        pulls: [{ number: 7, head: { sha: 'new-sha' } }],
        statuses: { 'master-sha': prior('master'), 'old-sha': prior('pr-7') }
    });
    await schedule(f.args);
    assert.equal(f.dispatched.length, 3);
    assert.ok(f.dispatched.every(run => run.inputs.ci_sha === 'new-sha'));
});

test('targets sharing a SHA and partially queued platforms are independent', async () => {
    const f = fixture({
        pulls: [{ number: 1, head: { sha: 'master-sha' } }],
        statuses: { 'master-sha': [...prior('master'), prior('pr-1')[0]] }
    });
    await schedule(f.args);
    assert.deepEqual(f.dispatched.map(run => run.workflow_id), ['Windows.yml', 'MacOS.yml']);
    assert.ok(f.dispatched.every(run => run.inputs.ci_target === 'pr-1'));
});

test('failed dispatch is explicit, does not block other platforms, and can be retried', async () => {
    const f = fixture({ failDispatch: run => run.workflow_id === 'Windows.yml' });
    await schedule(f.args);
    assert.equal(f.dispatched.length, 2);
    assert.equal(f.errors.length, 1);
    assert.equal(f.failures.length, 1);
    const failed = f.reported.find(status => status.state === 'error');
    assert.equal(failed.description, 'Daily CI dispatch failed');

    const retry = fixture({
        statuses: { 'master-sha': [failed, ...prior('master')] }
    });
    await schedule(retry.args);
    assert.deepEqual(retry.dispatched.map(run => run.workflow_id), ['Windows.yml']);
});

test('status API errors fail the scheduler rather than dispatching duplicate builds', async () => {
    const f = fixture();
    f.args.github.paginate = async method => {
        if (method === 'pulls') return [];
        throw new Error('Status API unavailable');
    };
    await assert.rejects(schedule(f.args), /Status API unavailable/);
    assert.equal(f.dispatched.length, 0);
});

test('build setup isolates target sources from trusted local actions', () => {
    for (const [file, platform] of [
        ['Linux.yml', 'linux'], ['Windows.yml', 'windows'], ['MacOS.yml', 'macos']
    ]) {
        const workflow = readFileSync(path.join(__dirname, '../workflows', file), 'utf8');
        assert.doesNotMatch(workflow, /^\s+(schedule|push|pull_request|check_date):/m);
        const checkouts = [...workflow.matchAll(/uses: actions\/checkout@v5\s+with:\s+ref: ([^\r\n]+)\s+persist-credentials: false/g)];
        assert.equal(checkouts.length, platform === 'macos' ? 2 : platform === 'windows' ? 5 : 4);
        assert.ok(checkouts.every(match => match[1] === '${{ github.sha }}'));
        const setup = readFileSync(path.join(__dirname, '../actions/build-setup', platform, 'action.yml'), 'utf8');
        assert.match(setup, /ref: \$\{\{ github.event.inputs.ci_sha \|\| github.sha \}\}/);
        assert.match(setup, /persist-credentials: false/);
        assert.match(setup, /path: ci-source/);
        assert.match(setup, /source-directory: ci-source/);
        assert.doesNotMatch(setup, /git clean|git reset/);
        const build = readFileSync(path.join(__dirname, '../actions/cmake-build', platform, 'action.yml'), 'utf8');
        const presets = [...build.matchAll(/(?:configure|build|test)Preset:/g)];
        const sources = [...build.matchAll(/cmakeListsTxtPath: \$\{\{ github.workspace \}\}\/ci-source\/CMakeLists.txt/g)];
        assert.equal(sources.length, presets.length);
        assert.match(workflow, /permissions:\s+contents: read/);
        assert.match(workflow, /output_job:[\s\S]+permissions:\s+statuses: write/);
    }
});

test('vcpkg setup bootstraps and hashes the selected source checkout', () => {
    const setup = readFileSync(path.join(__dirname, '../actions/vcpkg-setup/action.yml'), 'utf8');
    assert.equal([...setup.matchAll(/working-directory: \$\{\{ inputs.source-directory \}\}/g)].length, 3);
    assert.match(setup, /SOURCE_DIRECTORY: \$\{\{ inputs.source-directory \}\}/);
    assert.match(setup, /str\(source \/ "vcpkg"\)/);
    assert.equal([...setup.matchAll(/format\('\{0\}\/vcpkg.json', inputs.source-directory\)/g)].length, 2);
});

test('targeted Windows validation does not overwrite full daily-matrix status', () => {
    const workflow = readFileSync(path.join(__dirname, '../workflows/Windows.yml'), 'utf8');
    assert.match(workflow, /info_job:\s+if: \$\{\{ !inputs.validation_only \}\}/);
    assert.match(workflow, /Report commit status\s+if: \$\{\{ !inputs.validation_only && inputs.ci_sha != '' && inputs.ci_target != '' \}\}/);
    assert.doesNotMatch(workflow, /needs\.check_date/);
    assert.match(workflow, /ctest --preset test-windows-msvc-debug-editor-instrumentation/);
    assert.equal([...workflow.matchAll(/working-directory: ci-source/g)].length, 3);
});

test('trusted reporting jobs attach success and failure to the target SHA', async () => {
    const AsyncFunction = Object.getPrototypeOf(async function () {}).constructor;
    for (const [file, platform] of [
        ['Linux.yml', 'linux'], ['Windows.yml', 'windows'], ['MacOS.yml', 'macos']
    ]) {
        const workflow = readFileSync(path.join(__dirname, '../workflows', file), 'utf8');
        const inline = workflow.match(/script: \|\r?\n([\s\S]+?)\r?\n      - name: Retrieve results/)[1];
        const report = new AsyncFunction('github', 'context', 'process', inline);
        for (const result of ['success', 'failure', 'cancelled', 'skipped']) {
            const f = fixture();
            await report(f.args.github, f.args.context, {
                env: {
                    CI_SHA: 'pr-head-sha', CI_TARGET: 'pr-17',
                    JOB_RESULTS: JSON.stringify({ build: { result }, info: { result: 'success' } })
                }
            });
            assert.equal(f.reported[0].sha, 'pr-head-sha');
            assert.equal(f.reported[0].context, `daily-ci/${platform}/pr-17`);
            assert.equal(f.reported[0].state, result === 'success' ? 'success' : 'failure');
        }
    }
});
