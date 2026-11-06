// Copyright (c) 2026 DIVIDE-Studio
const workflows = [
    { file: 'Linux.yml', platform: 'linux' },
    { file: 'Windows.yml', platform: 'windows' },
    { file: 'MacOS.yml', platform: 'macos' }
];

module.exports = async function schedule({ github, context, core }) {
    const repo = context.repo;
    const { data: repository } = await github.rest.repos.get(repo);
    const manual = context.eventName === 'workflow_dispatch';
    const pullTarget = pull => ({
        name: `pr-${pull.number}`, sha: pull.head.sha,
        trusted: pull.head.repo?.full_name === `${repo.owner}/${repo.repo}`
    });
    let targets;
    if (manual) {
        const input = (context.payload.inputs?.pull_request ?? '').trim();
        const match = input.match(/^#?([1-9]\d*)$/);
        const pullNumber = match ? Number(match[1]) : NaN;
        if (!Number.isSafeInteger(pullNumber)) {
            throw new Error('Enter a valid pull request number.');
        }
        const { data: pull } = await github.rest.pulls.get({ ...repo, pull_number: pullNumber });
        if (pull.state !== 'open') {
            throw new Error(`Pull request #${pullNumber} is not open.`);
        }
        targets = [pullTarget(pull)];
    } else {
        const { data: master } = await github.rest.repos.getBranch({ ...repo, branch: 'master' });
        const pulls = await github.paginate(github.rest.pulls.list, {
            ...repo, state: 'open', per_page: 100
        });
        targets = [
            { name: 'master', sha: master.commit.sha },
            ...pulls.map(pullTarget)
        ];
    }
    let dispatchFailed = false;
    if (manual && !targets[0].trusted) {
        core.error(`Cannot run CI for ${targets[0].name}: the head repository is a fork or unavailable.`);
        dispatchFailed = true;
    }

    for (const target of targets) {
        const statuses = await github.paginate(github.rest.repos.listCommitStatusesForRef, {
            ...repo, ref: target.sha, per_page: 100
        });
        for (const workflow of workflows) {
            const statusContext = `daily-ci/${workflow.platform}/${target.name}`;
            if (target.name !== 'master' && !target.trusted) {
                const description = 'Fork builds require PR-isolated validation';
                const previous = statuses.find(status => status.context === statusContext);
                if (previous?.description !== description) {
                    await github.rest.repos.createCommitStatus({
                        ...repo, sha: target.sha, context: statusContext,
                        state: 'error', description
                    });
                }
                core.info(`Not executing untrusted ${target.name} with default-branch cache credentials.`);
                continue;
            }
            // The API returns newest statuses first. Failed dispatches may be retried.
            const previous = statuses.find(status => status.context === statusContext);
            const retryFailed = manual && (previous?.state === 'failure' || previous?.state === 'error');
            if (previous && previous.description !== 'Daily CI dispatch failed' && !retryFailed) {
                core.info(`Skipping ${statusContext}: ${target.sha} is already ${previous.state}.`);
                continue;
            }

            await github.rest.repos.createCommitStatus({
                ...repo, sha: target.sha, context: statusContext,
                state: 'pending', description: 'Daily build queued',
                target_url: `${context.serverUrl}/${repo.owner}/${repo.repo}/actions/runs/${context.runId}`
            });
            try {
                // Run trusted workflow definitions, but build the selected target SHA.
                await github.rest.actions.createWorkflowDispatch({
                    ...repo, workflow_id: workflow.file, ref: repository.default_branch,
                    inputs: { ci_sha: target.sha, ci_target: target.name }
                });
                core.info(`Queued ${statusContext}: ${target.sha}`);
            } catch (error) {
                dispatchFailed = true;
                core.error(`Failed to dispatch ${statusContext}: ${error.message}`);
                await github.rest.repos.createCommitStatus({
                    ...repo, sha: target.sha, context: statusContext,
                    state: 'error', description: 'Daily CI dispatch failed'
                });
            }
        }
    }
    if (dispatchFailed) {
        core.setFailed('One or more daily CI workflows could not be dispatched.');
    }
};
