Contributing to the aktualizr project
=====

We welcome code contributions from the community and are very happy to receive feedback, issue reports and code in the form of pull requests.

Issue Tracker
----

This project uses the Github issue tracker. Please use that for bug reports and to discuss new features prior to implementation.

Code quality and style
----

All code should be developed according to the [Google C++ style guide](https://google.github.io/styleguide/cppguide.html). In addition, the code should conform to the following guidelines:

* The best way to ensure new features don't get broken is to test them in Aktualizr's CI.
* Formatting, static analysis and green tests are enforced by CI.
* The master branch should always be in a deployable state. Functionally incomplete code is allowed in master as long as it doesn't break other things, and is better than long-lived feature branches.
* Aim to keep PRs small.
* Make separate commits for logically separate changes within a PR.
   - Each commit should be buildable and should pass the tests (to simplify git bisect).
   - The short description (first line of the commit text) should not exceed 72 chars. The rest of the text (if applicable) should be separated by an empty line.
* Bugs should be reproduced with a failing test case before they are resolved.

Reviewing Changes
-----------------
Aktualizr is developed by and for a group of developers that span different organizations with different goals and needs. We want to make rapid progress--upstream first!--while not breaking each other. To balance these we:

* Require all code changes to have a +1 review by someone who isn't the author.
* Prefer the reviewer to have a different organizational affiliation to the author
* After 3 business days, a +1 from anyone except the author is enough
* In all cases the CI should be green before merging
* Reviews within the same organization are encouraged. This might mean a PR gets two reviews. That is good.


Making a Pull Request
----

When you start developing a feature, please create a feature branch that includes the type of branch, the ID of the github issue or Jira ticket if available, and a brief description. For example `feat/9/https-support`, `fix/OTA-123/fix-token-expiry` or `refactor/tidy-up-imports`. Please do not mix feature development, bugfixes and refactoring into the same branch.

When your feature is ready, push the branch and make a pull request. We will review the request and give you feedback. Once the code passes the review it can be merged into master and the branch can be deleted.

Continuous Integration (CI)
----

We use GitHub's CI. The configuration is in `.github/workflows`. Changes to that go through the same review process as other code changes.


Developer Certificate of Origin (DCO)
----

All commits in pull requests must contain a `Signed-off-by:` line to indicate that the developer has agreed to the terms of the [Developer Certificate of Origin](https://developercertificate.org):

~~~~
Developer's Certificate of Origin 1.1

By making a contribution to this project, I certify that:

(a) The contribution was created in whole or in part by me and I
    have the right to submit it under the open source license
    indicated in the file; or

(b) The contribution is based upon previous work that, to the best
    of my knowledge, is covered under an appropriate open source
    license and I have the right under that license to submit that
    work with modifications, whether created in whole or in part
    by me, under the same open source license (unless I am
    permitted to submit under a different license), as indicated
    in the file; or

(c) The contribution was provided directly to me by some other
    person who certified (a), (b) or (c) and I have not modified
    it.

(d) I understand and agree that this project and the contribution
    are public and that a record of the contribution (including all
    personal information I submit with it, including my sign-off) is
    maintained indefinitely and may be redistributed consistent with
    this project or the open source license(s) involved.
~~~~

A simple way to sign off is to use the `-s` flag of `git commit`.

New pull requests will automatically be checked by the [probot/dco](https://probot.github.io/apps/dco/).
