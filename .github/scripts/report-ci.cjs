// Copyright (c) 2026 DIVIDE-Studio
const configurations = (prefix, modes) =>
    ['editor', 'game'].flatMap(type => modes.map(mode => `${prefix} ${type} ${mode}`));

const jobNames = {
    linux: [
        ...configurations('Ubuntu latest - gcc', ['debug', 'release', 'profile']),
        ...configurations('Ubuntu latest - clang', ['debug', 'release']),
        'Ubuntu - clang ASAN+UBSAN (debug/editor)',
        'Ubuntu - gcc ASAN+UBSAN (debug/editor)'
    ],
    windows: [
        ...configurations('Windows - msvc', ['debug', 'release', 'profile']),
        ...configurations('Windows - clang', ['debug', 'release']),
        'Windows - msvc ASAN+UBSAN (debug/editor)',
        'Windows - clang ASAN+UBSAN (profile/editor)',
        'Windows - MSVC Debug/editor instrumentation validation'
    ],
    macos: [
        ...configurations('macOS x86_64 - Clang', ['debug', 'release']),
        ...configurations('macOS arm64 - Clang', ['debug', 'release'])
    ]
};

module.exports = async function report({ github, context, core, platform }) {
    const names = jobNames[platform];
    if (!names) {
        throw new Error(`Unknown CI platform: ${platform}`);
    }
    const jobs = await github.paginate(github.rest.actions.listJobsForWorkflowRun, {
        ...context.repo, run_id: context.runId, filter: 'latest', per_page: 100
    });
    const targetUrl = `${context.serverUrl}/${context.repo.owner}/${context.repo.repo}/actions/runs/${context.runId}`;
    let matrixSuccess = true;
    for (const name of names) {
        const matches = jobs.filter(job => job.name === name);
        const job = matches.length === 1 ? matches[0] : undefined;
        const success = job?.status === 'completed' && job.conclusion === 'success';
        const description = job
            ? `Matrix job: ${job.conclusion ?? job.status}`
            : 'Expected matrix job missing or duplicated';
        await github.rest.repos.createCommitStatus({
            ...context.repo, sha: process.env.CI_SHA, context: name,
            state: success ? 'success' : 'failure',
            description, target_url: job?.html_url ?? targetUrl
        });
        if (!success) {
            matrixSuccess = false;
            core.error(`${name}: ${description}`);
        }
    }
    const success = matrixSuccess && Object.values(JSON.parse(process.env.JOB_RESULTS))
        .every(job => job.result === 'success');
    await github.rest.repos.createCommitStatus({
        ...context.repo, sha: process.env.CI_SHA,
        context: `daily-ci/${platform}/${process.env.CI_TARGET}`,
        state: success ? 'success' : 'failure',
        description: success ? 'Daily build passed' : 'Daily build did not complete successfully',
        target_url: targetUrl
    });
    if (!success) {
        core.setFailed('One or more required CI jobs did not complete successfully.');
    }
};
