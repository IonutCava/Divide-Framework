const workflows = [
    { file: 'Linux.yml', platform: 'linux' },
    { file: 'Windows.yml', platform: 'windows' },
    { file: 'MacOS.yml', platform: 'macos' }
];

module.exports = async function schedule({ github, context, core }) {
    const repo = context.repo;
    const { data: repository } = await github.rest.repos.get(repo);
    const { data: master } = await github.rest.repos.getBranch({ ...repo, branch: 'master' });
    const pulls = await github.paginate(github.rest.pulls.list, {
        ...repo, state: 'open', per_page: 100
    });
    const targets = [
        { name: 'master', sha: master.commit.sha },
        ...pulls.map(pull => ({ name: `pr-${pull.number}`, sha: pull.head.sha }))
    ];
    let dispatchFailed = false;

    for (const target of targets) {
        const statuses = await github.paginate(github.rest.repos.listCommitStatusesForRef, {
            ...repo, ref: target.sha, per_page: 100
        });
        for (const workflow of workflows) {
            const statusContext = `daily-ci/${workflow.platform}/${target.name}`;
            // The API returns newest statuses first. Failed dispatches may be retried.
            const previous = statuses.find(status => status.context === statusContext);
            if (previous && previous.description !== 'Daily CI dispatch failed') {
                core.info(`Skipping ${statusContext}: ${target.sha} was already queued.`);
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
